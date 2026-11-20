/**
 * @brief PS Vita video driver (vita2d / GXM).
 *
 * The game submits up to a few hundred quads per frame. They are queued, sorted far-to-near and drawn with
 * vita2d's stock shaders, so no runtime shader compiler is needed.
 *
 * Paletted textures stay paletted on the GPU (P8 + a 256-entry palette), like on PS2 and PSP. Every game
 * palette lives in its own slot of one GPU-mapped block, and a draw call picks its palette by pointing a
 * copy of the texture's control words at that slot. Changing a palette or reusing a texture with another
 * palette therefore costs nothing.
 *
 * Differences from the OpenGL / SDL GPU backends:
 * - Those use a depth buffer and discard fully transparent fragments. vita2d's shaders can't discard, so
 *   quads are drawn back to front instead (stable sort by z). The result is the same for opaque pixels.
 *   A translucent pixel that is drawn before something behind it blends with that later pixel here, while
 *   the depth-buffer backends hide the later pixel. In practice this only changes the shade of a few
 *   shadow and HUD pixels.
 */

#if CRS_VIDEO_DRIVER_VITA

#include "platform/video/vita/vita_renderer.h"
#include "platform/video/vita/vita_crt.h"
#include "platform/video/vita/vita_shader.h"

#include "common.h"
#include "port/utils.h"
#include "sf33rd/AcrSDK/common/plcommon.h"
#include "sf33rd/AcrSDK/ps2/flps2etc.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"
#include "sf33rd/Source/Game/system/work_sys.h"

#include <SDL3/SDL.h>

#include <libgraph.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <vita2d.h>

#include <stdbool.h>
#include <string.h>

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define GAME_WIDTH 384
#define GAME_HEIGHT 224

/// The offscreen canvas is this many times the game's resolution
#define CANVAS_SCALE 2
#define CANVAS_WIDTH (GAME_WIDTH * CANVAS_SCALE)
#define CANVAS_HEIGHT (GAME_HEIGHT * CANVAS_SCALE)

/// The desktop backends allow 1024 quads per frame. Leave headroom instead of asserting
#define QUADS_MAX 2048
#define VERTICES_PER_QUAD 6

#define PALETTE_COLORS 256
#define PALETTE_BYTES (PALETTE_COLORS * sizeof(Uint32))

/// Texture storage. The attract mode peaks at about 11 MB
#define ARENA_SIZE (32 * 1024 * 1024)
#define ARENA_GRANULARITY 4096
#define ARENA_BLOCKS_MAX (FL_TEXTURE_MAX * 2 + 2)

/// Scratch memory for one frame's vertices and uniforms
#define POOL_SIZE (2 * 1024 * 1024)

#define CDRAM_GRANULARITY (256 * 1024)
#define USER_RW_GRANULARITY (4 * 1024)

#define Z_DRAW 0.5f
#define COLOR_BLACK 0xFF000000u
/// The screen is cleared to black with zero alpha. The alpha channel of the screen is never shown, and the
/// filter pass uses it to mix the filtered and the plain picture
#define COLOR_CLEAR 0x00000000u
#define COLOR_WHITE 0xFFFFFFFFu

typedef struct VitaQuad {
    float x[4];
    float y[4];
    float s[4];
    float t[4];
    float z;
    Uint32 color;   ///< RGBA8 as vita2d wants it (red in the low byte)
    Uint16 texture; ///< Texture handle (index + 1), 0 for a solid quad
    Uint16 palette; ///< Palette handle (index + 1), 0 if the texture isn't paletted
} VitaQuad;

typedef struct VitaTexture {
    SceGxmTexture gxm;
    Uint32 offset;   ///< Offset of the pixels in the arena
    Uint32 capacity; ///< Size of the arena block, 0 if there is none
    bool valid;
    bool paletted;
} VitaTexture;

typedef struct ArenaBlock {
    Uint32 offset;
    Uint32 size;
    bool used;
} ArenaBlock;

static bool initialized = false;

static Uint8* arena = NULL;
static ArenaBlock arena_blocks[ARENA_BLOCKS_MAX];
static int arena_block_count = 0;

static Uint32* palettes = NULL; ///< FL_PALETTE_MAX slots of PALETTE_COLORS entries
static VitaTexture textures[FL_TEXTURE_MAX];

static VitaQuad quads[QUADS_MAX];
static int quad_count = 0;
static Uint16 order[QUADS_MAX];
static Uint16 order_scratch[QUADS_MAX];

/// One per draw call, so that the control words handed to GXM stay untouched until the scene is rendered
static vita2d_texture batch_textures[QUADS_MAX];
static int batch_texture_count = 0;

static Uint16 current_texture = 0;
static Uint16 current_palette = 0;

static vita2d_texture* canvas = NULL;
/// Draw straight to the screen when no filter is chosen (the "nearest" setting)
static bool draw_direct = false;

typedef enum VitaFilterKind {
    FILTER_OFF,
    FILTER_SHADER, ///< A shader from Emu4VitaPlus, in app0:shaders/
    FILTER_CRT,    ///< A pattern from vita_crt.c
} VitaFilterKind;

typedef struct VitaShaderInfo {
    const char* id; ///< Value of the config setting, and for a shader its file name in app0:shaders/
    VitaFilterKind kind;
    bool linear;         ///< Shader: sample the picture with bilinear filtering, as its RetroArch preset does
    VitaCrtLook crt_look; ///< CRT: which pattern
} VitaShaderInfo;

/// The filters, in the order of the FILTER row of the Screen Adjust page (`Letter_Data_63` in eff63.c holds the
/// names shown there). Emu4VitaPlus's scale2xplus, crt-gdv-mini, crt-easymode, crt-lottes-fast and crt-lottes
/// dropped frames on hardware, so the CRT looks come from vita_crt.c instead
static const VitaShaderInfo shader_infos[] = {
    { "off", FILTER_OFF },
    { "sharp-bilinear", FILTER_SHADER, true },
    { "sharp-bilinear-simple", FILTER_SHADER, true },
    { "advanced-aa", FILTER_SHADER, false },
    { "lcd3x", FILTER_SHADER, false },
    { "zfast_lcd", FILTER_SHADER, true },
    { "zfast_crt", FILTER_SHADER, true },
    { "crt-scanlines", FILTER_CRT, false, VITA_CRT_SCANLINES },
    { "crt-scan-soft", FILTER_CRT, false, VITA_CRT_SCAN_SOFT },
    { "crt-scan-hard", FILTER_CRT, false, VITA_CRT_SCAN_HARD },
    { "crt-aperture", FILTER_CRT, false, VITA_CRT_APERTURE },
    { "crt-shadow-mask", FILTER_CRT, false, VITA_CRT_SHADOW_MASK },
    { "crt-slot-mask", FILTER_CRT, false, VITA_CRT_SLOT_MASK },
    { "gba-color", FILTER_SHADER, false },
    { "gbc-color", FILTER_SHADER, false },
    { "gba-color+lcd3x", FILTER_SHADER, false },
    { "gb-palette-dmg", FILTER_SHADER, false },
    { "gb-palette-pocket", FILTER_SHADER, false },
    { "gb-palette-light", FILTER_SHADER, false },
};

static int shader_index = 0;
static int shader_intensity = 100;
static bool shader_loaded = false;
static VitaShader shader;
static SceGxmTexture shader_texture;
static vita2d_pgf* message_font = NULL;

static VitaAspect current_aspect = VITA_ASPECT_4_3;

/// Where the picture goes on the screen for the chosen aspect
static SDL_Rect base_view = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
/// The same after the game's Screen Adjust option has moved and shrunk it
static SDL_Rect view = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };

/// Game coordinates to vita2d coordinates (vita2d always works in 960x544 units, whatever the target is)
static float scale_x = 1.0f;
static float scale_y = 1.0f;
static float offset_x = 0.0f;
static float offset_y = 0.0f;

// GPU memory

static void* gpu_alloc(Uint32 size, const char* name) {
    SceKernelMemBlockType type = SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
    Uint32 aligned_size = ALIGN_UP(size, CDRAM_GRANULARITY);
    SceUID uid = sceKernelAllocMemBlock(name, type, aligned_size, NULL);

    if (uid < 0) {
        // Out of video memory, main memory works too
        type = SCE_KERNEL_MEMBLOCK_TYPE_USER_RW;
        aligned_size = ALIGN_UP(size, USER_RW_GRANULARITY);
        uid = sceKernelAllocMemBlock(name, type, aligned_size, NULL);
    }

    if (uid < 0) {
        SDL_Log("Couldn't allocate %u bytes of GPU memory for %s: 0x%08X", (unsigned)size, name, (unsigned)uid);
        return NULL;
    }

    void* base = NULL;

    if (sceKernelGetMemBlockBase(uid, &base) < 0) {
        return NULL;
    }

    if (sceGxmMapMemory(base, aligned_size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE) < 0) {
        SDL_Log("Couldn't map GPU memory for %s", name);
        return NULL;
    }

    return base;
}

// Texture arena. First fit over a list of blocks sorted by offset

static void arena_reset() {
    arena_blocks[0] = (ArenaBlock) { .offset = 0, .size = ARENA_SIZE, .used = false };
    arena_block_count = 1;
}

static void arena_insert_block(int index, ArenaBlock block) {
    memmove(&arena_blocks[index + 1], &arena_blocks[index], (arena_block_count - index) * sizeof(ArenaBlock));
    arena_blocks[index] = block;
    arena_block_count += 1;
}

static void arena_remove_block(int index) {
    memmove(&arena_blocks[index], &arena_blocks[index + 1], (arena_block_count - index - 1) * sizeof(ArenaBlock));
    arena_block_count -= 1;
}

/// @return `true` and the offset of the new block, or `false` if the arena is full
static bool arena_alloc(Uint32 size, Uint32* offset) {
    size = ALIGN_UP(size, ARENA_GRANULARITY);

    for (int i = 0; i < arena_block_count; i++) {
        ArenaBlock* block = &arena_blocks[i];

        if (block->used || block->size < size) {
            continue;
        }

        if (block->size > size && arena_block_count < ARENA_BLOCKS_MAX) {
            const ArenaBlock rest = { .offset = block->offset + size, .size = block->size - size, .used = false };
            block->size = size;
            arena_insert_block(i + 1, rest);
        }

        arena_blocks[i].used = true;
        *offset = arena_blocks[i].offset;
        return true;
    }

    return false;
}

static void arena_free(Uint32 offset) {
    for (int i = 0; i < arena_block_count; i++) {
        if (arena_blocks[i].offset != offset) {
            continue;
        }

        arena_blocks[i].used = false;

        // Merge with the free neighbours
        if (i + 1 < arena_block_count && !arena_blocks[i + 1].used) {
            arena_blocks[i].size += arena_blocks[i + 1].size;
            arena_remove_block(i + 1);
        }

        if (i > 0 && !arena_blocks[i - 1].used) {
            arena_blocks[i - 1].size += arena_blocks[i].size;
            arena_remove_block(i);
        }

        return;
    }
}

// Pixel conversion

/// 0xAARRGGBB to vita2d's RGBA8 (0xAABBGGRR)
static Uint32 argb_to_abgr(Uint32 color) {
    return (color & 0xFF00FF00u) | ((color >> 16) & 0xFFu) | ((color & 0xFFu) << 16);
}

static Uint32 expand_5_bits(Uint32 value) {
    return (value << 3) | (value >> 2);
}

/// PS2 16-bit color (red in the low bits, alpha in the top bit) to RGBA8
static Uint32 abgr1555_to_abgr8888(Uint16 color) {
    const Uint32 r = expand_5_bits(color & 0x1F);
    const Uint32 g = expand_5_bits((color >> 5) & 0x1F);
    const Uint32 b = expand_5_bits((color >> 10) & 0x1F);
    const Uint32 a = (color & 0x8000) ? 0xFF : 0;
    return r | (g << 8) | (b << 16) | (a << 24);
}

static Uint32* palette_slot(int palette_index) {
    return palettes + palette_index * PALETTE_COLORS;
}

// Quads

static VitaQuad* add_quad(float z) {
    if (quad_count >= QUADS_MAX) {
        return NULL;
    }

    VitaQuad* quad = &quads[quad_count];
    quad_count += 1;
    quad->z = z;
    return quad;
}

static void quad_set_texture(VitaQuad* quad) {
    quad->texture = current_texture;
    quad->palette = 0;

    if (current_texture > 0 && textures[current_texture - 1].paletted) {
        quad->palette = current_palette;
    }
}

/// Fill in corners 1 and 2 of an axis-aligned quad from corners 0 and 3
static void quad_infer_corners(VitaQuad* quad) {
    quad->x[1] = quad->x[3];
    quad->y[1] = quad->y[0];
    quad->x[2] = quad->x[0];
    quad->y[2] = quad->y[3];

    quad->s[1] = quad->s[3];
    quad->t[1] = quad->t[0];
    quad->s[2] = quad->s[0];
    quad->t[2] = quad->t[3];
}

/// Stable merge sort of `order` by z, far quads (large z) first
static void sort_quads() {
    bool sorted = true;

    for (int i = 0; i < quad_count; i++) {
        order[i] = i;

        if (i > 0 && quads[i - 1].z < quads[i].z) {
            sorted = false;
        }
    }

    if (sorted) {
        return;
    }

    Uint16* src = order;
    Uint16* dst = order_scratch;

    for (int width = 1; width < quad_count; width *= 2) {
        for (int start = 0; start < quad_count; start += 2 * width) {
            const int mid = SDL_min(start + width, quad_count);
            const int end = SDL_min(start + 2 * width, quad_count);
            int left = start;
            int right = mid;

            for (int out = start; out < end; out++) {
                // Take from the left run unless the right one is strictly farther, which keeps the sort stable
                if (left < mid && (right >= end || quads[src[left]].z >= quads[src[right]].z)) {
                    dst[out] = src[left++];
                } else {
                    dst[out] = src[right++];
                }
            }
        }

        Uint16* swap = src;
        src = dst;
        dst = swap;
    }

    if (src != order) {
        memcpy(order, src, quad_count * sizeof(Uint16));
    }
}

static float to_screen_x(float x) {
    return offset_x + x * scale_x;
}

static float to_screen_y(float y) {
    return offset_y + y * scale_y;
}

/// Corner order of the two triangles that make up a quad
static const int corner_order[VERTICES_PER_QUAD] = { 0, 1, 2, 2, 1, 3 };

/// @return Number of quads drawn, 0 if out of scratch memory
static int draw_textured_run(int first, int count) {
    const VitaQuad* head = &quads[order[first]];
    const VitaTexture* texture = &textures[head->texture - 1];

    if (batch_texture_count >= QUADS_MAX) {
        return 0;
    }

    vita2d_texture_vertex* vertices = vita2d_pool_memalign(
        count * VERTICES_PER_QUAD * sizeof(vita2d_texture_vertex), sizeof(vita2d_texture_vertex)
    );

    if (vertices == NULL) {
        return 0;
    }

    for (int i = 0; i < count; i++) {
        const VitaQuad* quad = &quads[order[first + i]];

        for (int j = 0; j < VERTICES_PER_QUAD; j++) {
            const int corner = corner_order[j];
            vita2d_texture_vertex* vertex = &vertices[i * VERTICES_PER_QUAD + j];
            vertex->x = to_screen_x(quad->x[corner]);
            vertex->y = to_screen_y(quad->y[corner]);
            vertex->z = Z_DRAW;
            vertex->u = quad->s[corner];
            vertex->v = quad->t[corner];
        }
    }

    vita2d_texture* batch_texture = &batch_textures[batch_texture_count];
    batch_texture_count += 1;
    batch_texture->gxm_tex = texture->gxm;

    if (texture->paletted) {
        sceGxmTextureSetPalette(&batch_texture->gxm_tex, palette_slot(head->palette - 1));
    }

    vita2d_draw_array_textured(
        batch_texture, SCE_GXM_PRIMITIVE_TRIANGLES, vertices, count * VERTICES_PER_QUAD, head->color
    );

    return count;
}

/// @return Number of quads drawn, 0 if out of scratch memory
static int draw_solid_run(int first, int count) {
    vita2d_color_vertex* vertices =
        vita2d_pool_memalign(count * VERTICES_PER_QUAD * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));

    if (vertices == NULL) {
        return 0;
    }

    for (int i = 0; i < count; i++) {
        const VitaQuad* quad = &quads[order[first + i]];

        for (int j = 0; j < VERTICES_PER_QUAD; j++) {
            const int corner = corner_order[j];
            vita2d_color_vertex* vertex = &vertices[i * VERTICES_PER_QUAD + j];
            vertex->x = to_screen_x(quad->x[corner]);
            vertex->y = to_screen_y(quad->y[corner]);
            vertex->z = Z_DRAW;
            vertex->color = quad->color;
        }
    }

    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, vertices, count * VERTICES_PER_QUAD);
    return count;
}

static bool quad_is_drawable(const VitaQuad* quad) {
    // The desktop backends clip against the depth range
    if (quad->z < -1.0f || quad->z > 1.0f) {
        return false;
    }

    if (quad->texture == 0) {
        return true;
    }

    const VitaTexture* texture = &textures[quad->texture - 1];

    if (!texture->valid) {
        return false;
    }

    return !texture->paletted || quad->palette != 0;
}

static bool quads_share_state(const VitaQuad* a, const VitaQuad* b) {
    if (a->texture != b->texture) {
        return false;
    }

    if (a->texture == 0) {
        // Solid quads carry their color in the vertices
        return true;
    }

    return a->palette == b->palette && a->color == b->color;
}

static void draw_quads() {
    int i = 0;

    while (i < quad_count) {
        const VitaQuad* head = &quads[order[i]];

        if (!quad_is_drawable(head)) {
            i += 1;
            continue;
        }

        int count = 1;

        while (i + count < quad_count) {
            const VitaQuad* next = &quads[order[i + count]];

            if (!quad_is_drawable(next) || !quads_share_state(head, next)) {
                break;
            }

            count += 1;
        }

        const int drawn = (head->texture == 0) ? draw_solid_run(i, count) : draw_textured_run(i, count);

        if (drawn == 0) {
            // Out of scratch memory. Drop the rest of the frame
            break;
        }

        i += count;
    }
}

static void draw_black_bars() {
    const int right = view.x + view.w;
    const int bottom = view.y + view.h;

    if (view.x > 0) {
        vita2d_draw_rectangle(0, 0, view.x, SCREEN_HEIGHT, COLOR_BLACK);
    }

    if (right < SCREEN_WIDTH) {
        vita2d_draw_rectangle(right, 0, SCREEN_WIDTH - right, SCREEN_HEIGHT, COLOR_BLACK);
    }

    if (view.y > 0) {
        vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, view.y, COLOR_BLACK);
    }

    if (bottom < SCREEN_HEIGHT) {
        vita2d_draw_rectangle(0, bottom, SCREEN_WIDTH, SCREEN_HEIGHT - bottom, COLOR_BLACK);
    }
}

static void set_transform_for_canvas() {
    // vita2d's projection maps 960x544 units onto whatever is being drawn to
    scale_x = (float)SCREEN_WIDTH / GAME_WIDTH;
    scale_y = (float)SCREEN_HEIGHT / GAME_HEIGHT;
    offset_x = 0;
    offset_y = 0;
}

static void set_transform_for_screen() {
    scale_x = (float)view.w / GAME_WIDTH;
    scale_y = (float)view.h / GAME_HEIGHT;
    offset_x = view.x;
    offset_y = view.y;
}

/// Apply the position and size from the game's Screen Adjust option.
/// The position is in game pixels (-10 to 10), the size in percent (94 to 100)
static void update_view() {
    const int size_h = SDL_clamp((int)Disp_Size_H, 50, 100);
    const int size_v = SDL_clamp((int)Disp_Size_V, 50, 100);

    view.w = base_view.w * size_h / 100;
    view.h = base_view.h * size_v / 100;
    view.x = base_view.x + (base_view.w - view.w) / 2 + X_Adjust * base_view.w / GAME_WIDTH;
    view.y = base_view.y + (base_view.h - view.h) / 2 + Y_Adjust * base_view.h / GAME_HEIGHT;
}

static SDL_Rect view_for_aspect(VitaAspect aspect) {
    int width = SCREEN_WIDTH;

    switch (aspect) {
    case VITA_ASPECT_4_3:
        width = SCREEN_HEIGHT * 4 / 3;
        break;

    case VITA_ASPECT_SQUARE_PIXELS:
        width = SCREEN_HEIGHT * GAME_WIDTH / GAME_HEIGHT;
        break;

    case VITA_ASPECT_STRETCH:
        width = SCREEN_WIDTH;
        break;
    }

    // Keep the bars the same width on both sides
    width &= ~1;

    return (SDL_Rect) { .x = (SCREEN_WIDTH - width) / 2, .y = 0, .w = width, .h = SCREEN_HEIGHT };
}

// Public

bool VitaRenderer_Init(VitaAspect aspect, VitaFilter filter) {
    if (initialized) {
        return true;
    }

    vita2d_init_advanced(POOL_SIZE);
    vita2d_set_clear_color(COLOR_CLEAR);
    vita2d_set_vblank_wait(1);

    arena = gpu_alloc(ARENA_SIZE, "3sx_textures");
    palettes = gpu_alloc(FL_PALETTE_MAX * PALETTE_BYTES, "3sx_palettes");

    if (arena == NULL || palettes == NULL) {
        return false;
    }

    arena_reset();
    memset(palettes, 0, FL_PALETTE_MAX * PALETTE_BYTES);
    SDL_zeroa(textures);

    current_aspect = aspect;
    base_view = view_for_aspect(aspect);
    view = base_view;

    // The canvas is also where the filters read the picture from, so it exists even for "nearest"
    // The alpha channel of the canvas is junk after blending, so read it back as opaque
    canvas = vita2d_create_empty_texture_rendertarget(CANVAS_WIDTH, CANVAS_HEIGHT, SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1BGR);
    draw_direct = (filter == VITA_FILTER_NEAREST);

    if (canvas != NULL) {
        const SceGxmTextureFilter scale_filter =
            draw_direct ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR;
        vita2d_texture_set_filters(canvas, scale_filter, scale_filter);
    } else {
        SDL_Log("Couldn't create the offscreen canvas, drawing straight to the screen");
    }

    initialized = true;
    return true;
}

void VitaRenderer_Shutdown() {
    if (!initialized) {
        return;
    }

    vita2d_wait_rendering_done();
    sceGxmDisplayQueueFinish();

    if (message_font != NULL) {
        vita2d_free_pgf(message_font);
        message_font = NULL;
    }

    if (shader_loaded) {
        VitaShader_Free(&shader);
        shader_loaded = false;
    }

    VitaCrt_Shutdown();

    if (canvas != NULL) {
        vita2d_free_texture(canvas);
        canvas = NULL;
    }

    vita2d_fini();
    initialized = false;
}

void VitaRenderer_BeginFrame() {
    quad_count = 0;
    batch_texture_count = 0;
}

void VitaRenderer_RenderFrame() {
    update_view();
    sort_quads();
    vita2d_pool_reset();

    const bool filtered = shader_loaded && shader_intensity > 0;
    const bool crt = shader_infos[shader_index].kind == FILTER_CRT && shader_intensity > 0;

    if (canvas != NULL && (filtered || crt || !draw_direct)) {
        set_transform_for_canvas();
        vita2d_start_drawing_advanced(canvas, 0);
        vita2d_clear_screen();
        draw_quads();
        vita2d_end_drawing();

        vita2d_start_drawing_advanced(NULL, 0);
        vita2d_clear_screen();

        if (!filtered || shader_intensity < 100) {
            // The plain picture. Adding to the cleared screen copies the canvas whatever its alpha channel
            // holds, and leaves 1 - intensity in the screen's alpha channel for the filter pass
            const Uint32 alpha = filtered ? (Uint32)(255 * (100 - shader_intensity) / 100) : 255;
            vita2d_set_blend_mode_add(1);
            vita2d_draw_texture_tint_scale(
                canvas,
                view.x,
                view.y,
                (float)view.w / CANVAS_WIDTH,
                (float)view.h / CANVAS_HEIGHT,
                RGBA8(255, 255, 255, alpha)
            );
            vita2d_set_blend_mode_add(0);
        }

        if (filtered) {
            // The canvas holds the picture at twice the game's resolution. The filters are told it is
            // 384x224 so that scanlines, masks and the like line up with the game's pixels
            VitaShader_Draw(
                &shader, &shader_texture, GAME_WIDTH, GAME_HEIGHT, 1.0f, 1.0f, view.x, view.y, view.w, view.h
            );
        }

        if (crt) {
            // The CRT looks apply the intensity themselves
            VitaCrt_Draw(shader_infos[shader_index].crt_look, shader_intensity, view.x, view.y, view.w, view.h);
        }

        vita2d_end_drawing();
    } else {
        set_transform_for_screen();
        vita2d_start_drawing_advanced(NULL, 0);
        vita2d_clear_screen();
        draw_quads();
        // Sprites may stick out of the picture
        draw_black_bars();
        vita2d_end_drawing();
    }
}

int VitaRenderer_GetFilterCount() {
    return SDL_arraysize(shader_infos);
}

const char* VitaRenderer_GetFilterId(int index) {
    if (index < 0 || index >= SDL_arraysize(shader_infos)) {
        return shader_infos[0].id;
    }

    return shader_infos[index].id;
}

int VitaRenderer_FindFilter(const char* id) {
    if (id == NULL) {
        return 0;
    }

    for (int i = 0; i < SDL_arraysize(shader_infos); i++) {
        if (SDL_strcmp(id, shader_infos[i].id) == 0) {
            return i;
        }
    }

    return 0;
}

bool VitaRenderer_SetFilter(int index) {
    if (index < 0 || index >= SDL_arraysize(shader_infos)) {
        index = 0;
    }

    if (index == shader_index && (index == 0 || shader_loaded)) {
        return true;
    }

    if (shader_loaded) {
        // The GPU may still be drawing the last frame with it
        vita2d_wait_rendering_done();
        VitaShader_Free(&shader);
        shader_loaded = false;
    }

    shader_index = index;

    if (index == 0) {
        return true;
    }

    if (canvas == NULL) {
        return false;
    }

    if (shader_infos[index].kind == FILTER_CRT) {
        return VitaCrt_Init();
    }

    if (!VitaShader_Load(&shader, shader_infos[index].id)) {
        return false;
    }

    const SceGxmTextureFilter sampling =
        shader_infos[index].linear ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
    shader_texture = canvas->gxm_tex;
    sceGxmTextureSetMinFilter(&shader_texture, sampling);
    sceGxmTextureSetMagFilter(&shader_texture, sampling);
    shader_loaded = true;
    return true;
}

int VitaRenderer_GetFilter() {
    return shader_index;
}

void VitaRenderer_SetFilterIntensity(int percent) {
    shader_intensity = SDL_clamp(percent, 0, 100);
}

int VitaRenderer_GetFilterIntensity() {
    return shader_intensity;
}

void VitaRenderer_SetAspect(VitaAspect aspect) {
    current_aspect = aspect;
    base_view = view_for_aspect(aspect);
}

VitaAspect VitaRenderer_GetAspect() {
    return current_aspect;
}

void VitaRenderer_EndFrame() {
    vita2d_swap_buffers();

    // Block until the frame is on the screen. This locks the game to the display's refresh rate, keeps
    // only one frame between reading the buttons and showing the result, and leaves the GPU idle while
    // the next frame's textures and palettes are written.
    sceGxmDisplayQueueFinish();
}

void VitaRenderer_ShowMessage(const char* text) {
    const float line_height = 26.0f;
    const float text_scale = 1.0f;

    if (message_font == NULL) {
        message_font = vita2d_load_default_pgf();
    }

    vita2d_pool_reset();
    vita2d_start_drawing_advanced(NULL, 0);
    vita2d_clear_screen();

    if (message_font != NULL) {
        char line[256];
        float y = 120.0f;
        const char* cursor = text;

        while (*cursor != '\0') {
            const char* end = SDL_strchr(cursor, '\n');
            const size_t length = (end != NULL) ? (size_t)(end - cursor) : SDL_strlen(cursor);
            SDL_strlcpy(line, cursor, SDL_min(length + 1, sizeof(line)));
            vita2d_pgf_draw_text(message_font, 60, (int)y, COLOR_WHITE, text_scale, line);
            y += line_height;
            cursor += length;

            if (*cursor == '\n') {
                cursor += 1;
            }
        }
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
    sceGxmDisplayQueueFinish();
}

void VitaRenderer_CreateTexture(unsigned int th) {
    const int texture_index = LO_16_BITS(th) - 1;

    if (texture_index < 0 || texture_index >= FL_TEXTURE_MAX) {
        fatal_error("Invalid texture handle: %u", th);
    }

    const FLTexture* fl_texture = &flTexture[texture_index];
    VitaTexture* texture = &textures[texture_index];
    const Uint8* src = flPS2GetSystemBuffAdrs(fl_texture->mem_handle);
    const int width = fl_texture->width;
    const int height = fl_texture->height;

    texture->valid = false;

    if (src == NULL || width <= 0 || height <= 0) {
        return;
    }

    bool paletted = false;

    switch (fl_texture->format) {
    case SCE_GS_PSMT8:
    case SCE_GS_PSMT4:
        paletted = true;
        break;

    case SCE_GS_PSMCT16:
    case SCE_GS_PSMCT32:
        paletted = false;
        break;

    default:
        fatal_error("Unhandled pixel format: %d", fl_texture->format);
        break;
    }

    // GXM wants rows that are a multiple of 8 pixels long
    const int stride = ALIGN_UP(width, 8);
    const int bytes_per_pixel = paletted ? 1 : 4;
    const Uint32 size = stride * height * bytes_per_pixel;

    if (texture->capacity < size) {
        if (texture->capacity > 0) {
            arena_free(texture->offset);
            texture->capacity = 0;
        }

        if (!arena_alloc(size, &texture->offset)) {
            SDL_Log("Out of texture memory (%dx%d, format %d)", width, height, (int)fl_texture->format);
            return;
        }

        texture->capacity = ALIGN_UP(size, ARENA_GRANULARITY);
    }

    Uint8* dst = arena + texture->offset;

    switch (fl_texture->format) {
    case SCE_GS_PSMT8:
        for (int y = 0; y < height; y++) {
            memcpy(dst + y * stride, src + y * width, width);
        }

        break;

    case SCE_GS_PSMT4:
        // Two pixels per byte, the low nibble comes first
        for (int y = 0; y < height; y++) {
            const Uint8* src_row = src + y * (width / 2);
            Uint8* dst_row = dst + y * stride;

            for (int x = 0; x < width / 2; x++) {
                dst_row[x * 2] = src_row[x] & 0xF;
                dst_row[x * 2 + 1] = src_row[x] >> 4;
            }
        }

        break;

    case SCE_GS_PSMCT16:
        for (int y = 0; y < height; y++) {
            const Uint8* src_row = src + y * width * 2;
            Uint32* dst_row = (Uint32*)(dst + y * stride * 4);

            for (int x = 0; x < width; x++) {
                const Uint16 color = src_row[x * 2] | (src_row[x * 2 + 1] << 8);
                dst_row[x] = abgr1555_to_abgr8888(color);
            }
        }

        break;

    case SCE_GS_PSMCT32:
        // Bytes are already in R, G, B, A order
        for (int y = 0; y < height; y++) {
            memcpy(dst + y * stride * 4, src + y * width * 4, width * 4);
        }

        break;
    }

    const SceGxmTextureFormat format =
        paletted ? SCE_GXM_TEXTURE_FORMAT_P8_ABGR : SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR;

    if (sceGxmTextureInitLinear(&texture->gxm, dst, format, width, height, 0) < 0) {
        SDL_Log("Couldn't create a %dx%d texture", width, height);
        return;
    }

    sceGxmTextureSetMinFilter(&texture->gxm, SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&texture->gxm, SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetUAddrMode(&texture->gxm, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&texture->gxm, SCE_GXM_TEXTURE_ADDR_CLAMP);

    texture->paletted = paletted;
    texture->valid = true;
}

void VitaRenderer_DestroyTexture(unsigned int texture_handle) {
    if (texture_handle == 0 || texture_handle > FL_TEXTURE_MAX) {
        return;
    }

    VitaTexture* texture = &textures[texture_handle - 1];

    if (texture->capacity > 0) {
        arena_free(texture->offset);
    }

    SDL_zerop(texture);
}

void VitaRenderer_UnlockTexture(unsigned int th) {
    VitaRenderer_CreateTexture(th);
}

void VitaRenderer_CreatePalette(unsigned int ph) {
    const int palette_index = HI_16_BITS(ph) - 1;

    if (palette_index < 0 || palette_index >= FL_PALETTE_MAX) {
        fatal_error("Invalid palette handle: %u", ph);
    }

    const FLTexture* fl_palette = &flPalette[palette_index];
    const Uint8* src = flPS2GetSystemBuffAdrs(fl_palette->mem_handle);
    const int color_count = SDL_min(fl_palette->width * fl_palette->height, PALETTE_COLORS);
    Uint32* dst = palette_slot(palette_index);

    if (src == NULL) {
        return;
    }

    switch (fl_palette->format) {
    case SCE_GS_PSMCT32:
        // Stored as B, G, R, A
        for (int i = 0; i < color_count; i++) {
            const Uint32 r = src[i * 4 + 2];
            const Uint32 g = src[i * 4 + 1];
            const Uint32 b = src[i * 4];
            const Uint32 a = src[i * 4 + 3];
            dst[i] = r | (g << 8) | (b << 16) | (a << 24);
        }

        break;

    case SCE_GS_PSMCT16:
        for (int i = 0; i < color_count; i++) {
            const Uint16 color = src[i * 2] | (src[i * 2 + 1] << 8);
            dst[i] = abgr1555_to_abgr8888(color);
        }

        break;

    default:
        fatal_error("Unhandled pixel format: %d", fl_palette->format);
        break;
    }
}

void VitaRenderer_DestroyPalette(unsigned int palette_handle) {
    // Do nothing
}

void VitaRenderer_UnlockPalette(unsigned int ph) {
    VitaRenderer_CreatePalette(ph << 16);
}

void VitaRenderer_SetTexture(unsigned int th) {
    const unsigned int texture_handle = LO_16_BITS(th);
    const unsigned int palette_handle = HI_16_BITS(th);

    current_texture = (texture_handle <= FL_TEXTURE_MAX) ? texture_handle : 0;
    current_palette = (palette_handle <= FL_PALETTE_MAX) ? palette_handle : 0;
}

void VitaRenderer_DrawTexturedQuad(const Sprite* sprite, unsigned int color) {
    VitaQuad* quad = add_quad(sprite->v[0].z);

    if (quad == NULL) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        quad->x[i] = sprite->v[i].x;
        quad->y[i] = sprite->v[i].y;
        quad->s[i] = sprite->t[i].s;
        quad->t[i] = sprite->t[i].t;
    }

    quad->color = argb_to_abgr(color);
    quad_set_texture(quad);
}

void VitaRenderer_DrawSprite(const Sprite* sprite, unsigned int color) {
    VitaQuad* quad = add_quad(sprite->v[0].z);

    if (quad == NULL) {
        return;
    }

    quad->x[0] = sprite->v[0].x;
    quad->y[0] = sprite->v[0].y;
    quad->x[3] = sprite->v[3].x;
    quad->y[3] = sprite->v[3].y;
    quad->s[0] = sprite->t[0].s;
    quad->t[0] = sprite->t[0].t;
    quad->s[3] = sprite->t[3].s;
    quad->t[3] = sprite->t[3].t;
    quad_infer_corners(quad);

    quad->color = argb_to_abgr(color);
    quad_set_texture(quad);
}

void VitaRenderer_DrawSprite2(const Sprite2* sprite2) {
    VitaQuad* quad = add_quad(sprite2->v[0].z);

    if (quad == NULL) {
        return;
    }

    quad->x[0] = sprite2->v[0].x;
    quad->y[0] = sprite2->v[0].y;
    quad->x[3] = sprite2->v[1].x;
    quad->y[3] = sprite2->v[1].y;
    quad->s[0] = sprite2->t[0].s;
    quad->t[0] = sprite2->t[0].t;
    quad->s[3] = sprite2->t[1].s;
    quad->t[3] = sprite2->t[1].t;
    quad_infer_corners(quad);

    quad->color = argb_to_abgr(sprite2->vertex_color);
    quad_set_texture(quad);
}

void VitaRenderer_DrawSolidQuad(const Quad* quad, unsigned int color) {
    VitaQuad* solid = add_quad(quad->v[0].z);

    if (solid == NULL) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        solid->x[i] = quad->v[i].x;
        solid->y[i] = quad->v[i].y;
        solid->s[i] = 0;
        solid->t[i] = 0;
    }

    solid->color = argb_to_abgr(color);
    solid->texture = 0;
    solid->palette = 0;
}

#endif // CRS_VIDEO_DRIVER_VITA
