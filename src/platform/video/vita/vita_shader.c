/**
 * @brief Post-processing filters for the PS Vita video driver.
 *
 * The programs are precompiled GXM binaries (the Vita has no shader compiler at runtime and VitaSDK has none
 * either). The loader follows libvita2d_ext by Francisco García (MIT), as used by Emu4VitaPlus.
 */

#if CRS_VIDEO_DRIVER_VITA

#include "platform/video/vita/vita_shader.h"

#include <SDL3/SDL.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <malloc.h>
#include <string.h>

#define SHADER_DIR "app0:shaders/"

/// vita2d's projection (960x544 units onto whatever is being drawn to)
extern float _vita2d_ortho_matrix[16];

static void* read_file(const char* path) {
    SceIoStat stat;

    if (sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0) {
        return NULL;
    }

    const SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);

    if (fd < 0) {
        return NULL;
    }

    const size_t size = (size_t)stat.st_size;
    void* data = memalign(16, size);
    const bool ok = (data != NULL) && (sceIoRead(fd, data, size) == (int)size);
    sceIoClose(fd);

    if (!ok) {
        free(data);
        return NULL;
    }

    return data;
}

static void* read_program(const char* name, const char* suffix) {
    char path[128];
    SDL_snprintf(path, sizeof(path), SHADER_DIR "%s%s", name, suffix);
    void* data = read_file(path);

    if (data == NULL) {
        SDL_Log("Couldn't read %s", path);
    } else if (sceGxmProgramCheck((const SceGxmProgram*)data) < 0) {
        SDL_Log("%s isn't a GXM program", path);
        free(data);
        data = NULL;
    }

    return data;
}

/// Mix with the unfiltered picture by the destination alpha, which holds 1 - intensity
static const SceGxmBlendInfo blend_by_dst_alpha = {
    .colorMask = SCE_GXM_COLOR_MASK_ALL,
    .colorFunc = SCE_GXM_BLEND_FUNC_ADD,
    .alphaFunc = SCE_GXM_BLEND_FUNC_ADD,
    .colorSrc = SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    .colorDst = SCE_GXM_BLEND_FACTOR_DST_ALPHA,
    .alphaSrc = SCE_GXM_BLEND_FACTOR_ZERO,
    .alphaDst = SCE_GXM_BLEND_FACTOR_ONE,
};

bool VitaShader_Load(VitaShader* shader, const char* name) {
    void* vertex_gxp = read_program(name, "_v.gxp");
    void* fragment_gxp = read_program(name, "_f.gxp");

    if (vertex_gxp == NULL || fragment_gxp == NULL) {
        free(vertex_gxp);
        free(fragment_gxp);
        SDL_zerop(shader);
        return false;
    }

    if (!VitaShader_LoadPrograms(shader, name, vertex_gxp, fragment_gxp, &blend_by_dst_alpha)) {
        free(vertex_gxp);
        free(fragment_gxp);
        return false;
    }

    // The patcher keeps pointing at the programs, so they live as long as the shader
    shader->vertex_gxp = vertex_gxp;
    shader->fragment_gxp = fragment_gxp;
    return true;
}

bool VitaShader_LoadPrograms(
    VitaShader* shader, const char* name, const SceGxmProgram* vertex_gxp, const SceGxmProgram* fragment_gxp,
    const SceGxmBlendInfo* blend
) {
    SceGxmShaderPatcher* patcher = vita2d_get_shader_patcher();
    SDL_zerop(shader);
    shader->fragment_program_gxp = fragment_gxp;

    if (sceGxmShaderPatcherRegisterProgram(patcher, vertex_gxp, &shader->vertex_id) < 0 ||
        sceGxmShaderPatcherRegisterProgram(patcher, fragment_gxp, &shader->fragment_id) < 0) {
        SDL_Log("Couldn't register the %s programs", name);
        VitaShader_Free(shader);
        return false;
    }

    const SceGxmProgramParameter* position = sceGxmProgramFindParameterByName(vertex_gxp, "aPosition");
    const SceGxmProgramParameter* texcoord = sceGxmProgramFindParameterByName(vertex_gxp, "aTexcoord");
    shader->wvp = sceGxmProgramFindParameterByName(vertex_gxp, "wvp");

    if (position == NULL || texcoord == NULL || shader->wvp == NULL) {
        SDL_Log("%s doesn't have the expected inputs", name);
        VitaShader_Free(shader);
        return false;
    }

    // vita2d_texture_vertex: x, y, z, u, v
    const SceGxmVertexAttribute attributes[2] = {
        { .streamIndex = 0,
          .offset = 0,
          .format = SCE_GXM_ATTRIBUTE_FORMAT_F32,
          .componentCount = 3,
          .regIndex = sceGxmProgramParameterGetResourceIndex(position) },
        { .streamIndex = 0,
          .offset = 12,
          .format = SCE_GXM_ATTRIBUTE_FORMAT_F32,
          .componentCount = 2,
          .regIndex = sceGxmProgramParameterGetResourceIndex(texcoord) },
    };
    const SceGxmVertexStream stream = { .stride = sizeof(vita2d_texture_vertex),
                                        .indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT };

    if (sceGxmShaderPatcherCreateVertexProgram(
            patcher, shader->vertex_id, attributes, 2, &stream, 1, &shader->vertex_program
        ) < 0 ||
        sceGxmShaderPatcherCreateFragmentProgram(
            patcher,
            shader->fragment_id,
            SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
            SCE_GXM_MULTISAMPLE_NONE,
            blend,
            vertex_gxp,
            &shader->fragment_program
        ) < 0) {
        SDL_Log("Couldn't create the %s programs", name);
        VitaShader_Free(shader);
        return false;
    }

    shader->vertex_texture_size = sceGxmProgramFindParameterByName(vertex_gxp, "IN.texture_size");
    shader->vertex_video_size = sceGxmProgramFindParameterByName(vertex_gxp, "IN.video_size");
    shader->vertex_output_size = sceGxmProgramFindParameterByName(vertex_gxp, "IN.output_size");
    shader->fragment_texture_size = sceGxmProgramFindParameterByName(fragment_gxp, "IN.texture_size");
    shader->fragment_video_size = sceGxmProgramFindParameterByName(fragment_gxp, "IN.video_size");
    shader->fragment_output_size = sceGxmProgramFindParameterByName(fragment_gxp, "IN.output_size");
    return true;
}

void VitaShader_Free(VitaShader* shader) {
    SceGxmShaderPatcher* patcher = vita2d_get_shader_patcher();

    if (shader->fragment_program != NULL) {
        sceGxmShaderPatcherReleaseFragmentProgram(patcher, shader->fragment_program);
    }

    if (shader->vertex_program != NULL) {
        sceGxmShaderPatcherReleaseVertexProgram(patcher, shader->vertex_program);
    }

    if (shader->fragment_id != NULL) {
        sceGxmShaderPatcherUnregisterProgram(patcher, shader->fragment_id);
    }

    if (shader->vertex_id != NULL) {
        sceGxmShaderPatcherUnregisterProgram(patcher, shader->vertex_id);
    }

    free(shader->vertex_gxp);
    free(shader->fragment_gxp);
    SDL_zerop(shader);
}

static void set_size(void* buffer, const SceGxmProgramParameter* parameter, const float* value) {
    if (parameter != NULL) {
        sceGxmSetUniformDataF(buffer, parameter, 0, 2, value);
    }
}

void VitaShader_Draw(
    const VitaShader* shader, const SceGxmTexture* texture, float source_w, float source_h, float u, float v, float x,
    float y, float w, float h
) {
    SceGxmContext* context = vita2d_get_context();
    const float texture_size[2] = { source_w, source_h };
    const float output_size[2] = { w, h };

    vita2d_texture_vertex* vertices = vita2d_pool_memalign(4 * sizeof(vita2d_texture_vertex), sizeof(vita2d_texture_vertex));

    if (vertices == NULL) {
        return;
    }

    vertices[0] = (vita2d_texture_vertex) { x, y, 0.5f, 0.0f, 0.0f };
    vertices[1] = (vita2d_texture_vertex) { x + w, y, 0.5f, u, 0.0f };
    vertices[2] = (vita2d_texture_vertex) { x, y + h, 0.5f, 0.0f, v };
    vertices[3] = (vita2d_texture_vertex) { x + w, y + h, 0.5f, u, v };

    sceGxmSetVertexProgram(context, shader->vertex_program);
    sceGxmSetFragmentProgram(context, shader->fragment_program);

    void* vertex_uniforms = NULL;
    sceGxmReserveVertexDefaultUniformBuffer(context, &vertex_uniforms);

    if (vertex_uniforms != NULL) {
        sceGxmSetUniformDataF(vertex_uniforms, shader->wvp, 0, 16, _vita2d_ortho_matrix);
        // The game picture is the whole of the texture, so the "video" and the texture are the same size
        set_size(vertex_uniforms, shader->vertex_texture_size, texture_size);
        set_size(vertex_uniforms, shader->vertex_video_size, texture_size);
        set_size(vertex_uniforms, shader->vertex_output_size, output_size);
    }

    // Programs without fragment uniforms have no buffer to reserve
    if (sceGxmProgramGetDefaultUniformBufferSize(shader->fragment_program_gxp) > 0) {
        void* fragment_uniforms = NULL;
        sceGxmReserveFragmentDefaultUniformBuffer(context, &fragment_uniforms);

        if (fragment_uniforms != NULL) {
            set_size(fragment_uniforms, shader->fragment_texture_size, texture_size);
            set_size(fragment_uniforms, shader->fragment_video_size, texture_size);
            set_size(fragment_uniforms, shader->fragment_output_size, output_size);
        }
    }

    sceGxmSetFragmentTexture(context, 0, texture);
    sceGxmSetVertexStream(context, 0, vertices);
    sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, vita2d_get_linear_indices(), 4);
}

#endif // CRS_VIDEO_DRIVER_VITA
