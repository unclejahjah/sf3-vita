/**
 * @brief Light CRT looks for the PS Vita video driver.
 *
 * The full CRT shaders (crt-lottes, crt-easymode, crt-gdv-mini) shade every pixel of the picture with 100-200 GPU
 * instructions and can't keep 60 fps on the Vita. Most of what makes a CRT look like one is a fixed pattern,
 * though: the gaps between scanlines and the phosphor mask. Here that pattern is worked out on the CPU, once, into a
 * texture the size of the picture, and the GPU only multiplies it onto the picture with vita2d's plain texture
 * shader. The pattern follows the masks of Timothy Lottes' public-domain CRT shader.
 */

#if CRS_VIDEO_DRIVER_VITA

#include "platform/video/vita/vita_crt.h"
#include "platform/video/vita/vita_shader.h"

#include <SDL3/SDL.h>

#include <psp2/gxm.h>
#include <vita2d.h>

#include <math.h>

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define GAME_HEIGHT 224

/// vita2d's own texture shaders
extern const SceGxmProgram texture_v_gxp_start;
extern const SceGxmProgram texture_f_gxp_start;

/// result = 2 * pattern * picture, so a pattern value of 0.5 (128) leaves the picture as it is and the pattern can
/// brighten as well as darken
static const SceGxmBlendInfo modulate_2x = {
    .colorMask = SCE_GXM_COLOR_MASK_ALL,
    .colorFunc = SCE_GXM_BLEND_FUNC_ADD,
    .alphaFunc = SCE_GXM_BLEND_FUNC_ADD,
    .colorSrc = SCE_GXM_BLEND_FACTOR_DST_COLOR,
    .colorDst = SCE_GXM_BLEND_FACTOR_SRC_COLOR,
    .alphaSrc = SCE_GXM_BLEND_FACTOR_ZERO,
    .alphaDst = SCE_GXM_BLEND_FACTOR_ONE,
};

/// How dark the gap between two scanlines gets (0 = no scanlines, 1 = black)
#define SCANLINE_DEPTH_ALONE 0.5f
#define SCANLINE_DEPTH_SOFT 0.3f
#define SCANLINE_DEPTH_HARD 0.75f
#define SCANLINE_DEPTH_WITH_MASK 0.35f

/// Phosphor mask: the lit colour and the two others
#define MASK_LIGHT 1.3f
#define MASK_DARK 0.7f

/// How much of the light lost to the scanlines and the mask is given back
#define BRIGHTNESS_RESTORE 0.8f

static bool initialized = false;
static VitaShader program;
static vita2d_texture* pattern = NULL;

/// What `pattern` holds now
static int pattern_look = -1;
static int pattern_intensity = -1;
static int pattern_w = 0;
static int pattern_h = 0;

static float row_light[SCREEN_HEIGHT];

/// The phosphor mask at screen pixel x, y: how much of each of red, green and blue gets through
static void mask_at(VitaCrtLook look, int x, int y, float rgb[3]) {
    const int lit = x % 3;
    float line = 1.0f;

    rgb[0] = rgb[1] = rgb[2] = MASK_DARK;
    rgb[lit] = MASK_LIGHT;

    switch (look) {
    case VITA_CRT_SCANLINES:
    case VITA_CRT_SCAN_SOFT:
    case VITA_CRT_SCAN_HARD:
        rgb[0] = rgb[1] = rgb[2] = 1.0f;
        break;

    case VITA_CRT_APERTURE:
        break;

    case VITA_CRT_SHADOW_MASK: {
        // Every other triad is shifted down by a row, and every other row of a triad is dimmed
        const int odd = (x / 3) & 1;

        if (((y + odd) & 1) == 0) {
            line = MASK_DARK;
        }

        break;
    }

    case VITA_CRT_SLOT_MASK: {
        // Slots four rows tall, with a dark row between them. Neighbouring triads are offset by two rows
        const int odd = (x / 3) & 1;

        if ((y + odd * 2) % 4 == 3) {
            line = MASK_DARK;
        }

        break;
    }
    }

    rgb[0] *= line;
    rgb[1] *= line;
    rgb[2] *= line;
}

static Uint8 encode(float multiplier) {
    // The blend doubles the pattern
    const float value = multiplier * 127.5f + 0.5f;
    return (Uint8)SDL_clamp(value, 0.0f, 255.0f);
}

static void build_pattern(VitaCrtLook look, int intensity, int w, int h) {
    float depth = SCANLINE_DEPTH_WITH_MASK;

    switch (look) {
    case VITA_CRT_SCANLINES:
        depth = SCANLINE_DEPTH_ALONE;
        break;

    case VITA_CRT_SCAN_SOFT:
        depth = SCANLINE_DEPTH_SOFT;
        break;

    case VITA_CRT_SCAN_HARD:
        depth = SCANLINE_DEPTH_HARD;
        break;

    default:
        break;
    }

    const float strength = intensity / 100.0f;
    float row_sum = 0.0f;

    // Brightness across one game line: full at its centre, darkest between two lines
    for (int y = 0; y < h; y++) {
        const float line_pos = (y + 0.5f) * GAME_HEIGHT / h;
        const float f = line_pos - floorf(line_pos);
        float gap = 0.5f + 0.5f * cosf(2.0f * SDL_PI_F * f);

        if (look == VITA_CRT_SCAN_HARD) {
            // Widen the dark part so the gaps read as solid lines
            gap = sqrtf(gap);
        }

        row_light[y] = 1.0f - depth * gap;
        row_sum += row_light[y];
    }

    // Average of the mask over one repeat (6 x 4 pixels covers every mask above)
    float mask_sum = 0.0f;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 6; x++) {
            float rgb[3];
            mask_at(look, x, y, rgb);
            mask_sum += (rgb[0] + rgb[1] + rgb[2]) / 3.0f;
        }
    }

    const float average = (row_sum / h) * (mask_sum / 24.0f);
    const float gain = 1.0f + BRIGHTNESS_RESTORE * (1.0f / average - 1.0f);

    Uint8* pixels = vita2d_texture_get_datap(pattern);
    const unsigned int stride = vita2d_texture_get_stride(pattern);

    for (int y = 0; y < h; y++) {
        // Whole words, since the texture is in GPU memory
        Uint32* out = (Uint32*)(pixels + y * stride);

        for (int x = 0; x < w; x++) {
            float rgb[3];
            Uint8 value[3];
            mask_at(look, x, y, rgb);

            for (int c = 0; c < 3; c++) {
                const float full = rgb[c] * row_light[y] * gain;
                value[c] = encode(1.0f + strength * (full - 1.0f));
            }

            out[x] = 0xFF000000u | ((Uint32)value[2] << 16) | ((Uint32)value[1] << 8) | value[0];
        }
    }

    pattern_look = look;
    pattern_intensity = intensity;
    pattern_w = w;
    pattern_h = h;
}

bool VitaCrt_Init() {
    if (initialized) {
        return true;
    }

    pattern = vita2d_create_empty_texture_format(SCREEN_WIDTH, SCREEN_HEIGHT, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);

    if (pattern == NULL) {
        SDL_Log("Couldn't create the CRT pattern");
        return false;
    }

    // One pattern pixel per screen pixel
    vita2d_texture_set_filters(pattern, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);

    if (!VitaShader_LoadPrograms(&program, "crt", &texture_v_gxp_start, &texture_f_gxp_start, &modulate_2x)) {
        vita2d_free_texture(pattern);
        pattern = NULL;
        return false;
    }

    pattern_look = -1;
    initialized = true;
    return true;
}

void VitaCrt_Shutdown() {
    if (!initialized) {
        return;
    }

    vita2d_wait_rendering_done();
    VitaShader_Free(&program);
    vita2d_free_texture(pattern);
    pattern = NULL;
    initialized = false;
}

void VitaCrt_Draw(VitaCrtLook look, int intensity, int x, int y, int w, int h) {
    if (!initialized || intensity <= 0) {
        return;
    }

    w = SDL_clamp(w, 1, SCREEN_WIDTH);
    h = SDL_clamp(h, 1, SCREEN_HEIGHT);

    if (look != pattern_look || intensity != pattern_intensity || w != pattern_w || h != pattern_h) {
        // Only when a setting changes. The GPU is idle here: the last frame was on the screen before this one began
        build_pattern(look, intensity, w, h);
    }

    VitaShader_Draw(
        &program,
        &pattern->gxm_tex,
        w,
        h,
        (float)w / SCREEN_WIDTH,
        (float)h / SCREEN_HEIGHT,
        (float)x,
        (float)y,
        (float)w,
        (float)h
    );
}

#endif // CRS_VIDEO_DRIVER_VITA
