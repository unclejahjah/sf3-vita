#ifndef VITA_SHADER_H
#define VITA_SHADER_H

#include <psp2/gxm.h>
#include <vita2d.h>

#include <stdbool.h>

/// A post-processing filter: a precompiled GXM vertex + fragment program pair in RetroArch's Cg convention
/// (`aPosition`, `aTexcoord`, `wvp` and the `IN.texture_size` / `IN.video_size` / `IN.output_size` uniforms).
/// The programs are the ones Emu4VitaPlus ships, loaded from `app0:shaders/<name>_v.gxp` and `<name>_f.gxp`.
typedef struct VitaShader {
    void* vertex_gxp;
    void* fragment_gxp;
    SceGxmShaderPatcherId vertex_id;
    SceGxmShaderPatcherId fragment_id;
    SceGxmVertexProgram* vertex_program;
    SceGxmFragmentProgram* fragment_program;
    const SceGxmProgramParameter* wvp;
    const SceGxmProgramParameter* vertex_texture_size;
    const SceGxmProgramParameter* vertex_video_size;
    const SceGxmProgramParameter* vertex_output_size;
    const SceGxmProgramParameter* fragment_texture_size;
    const SceGxmProgramParameter* fragment_video_size;
    const SceGxmProgramParameter* fragment_output_size;
} VitaShader;

/// Load a filter. Its output is mixed with what is already on the screen by the destination alpha:
/// result = filter * (1 - dst.a) + dst * dst.a, which is how the filter intensity is applied.
bool VitaShader_Load(VitaShader* shader, const char* name);

/// Free a filter. The GPU must be done with it (`vita2d_wait_rendering_done`)
void VitaShader_Free(VitaShader* shader);

/// Draw `texture` through the filter into the rectangle x, y, w, h (vita2d's 960x544 units).
/// `source_w` x `source_h` is the resolution the filter should treat the texture as having
void VitaShader_Draw(
    const VitaShader* shader, const SceGxmTexture* texture, float source_w, float source_h, float x, float y, float w,
    float h
);

#endif
