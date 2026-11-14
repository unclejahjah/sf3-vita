#ifndef VITA_RENDERER_H
#define VITA_RENDERER_H

#include "core/render_primitives.h"

#include <stdbool.h>

/// How the 384x224 picture is fitted to the 960x544 screen
typedef enum VitaAspect {
    VITA_ASPECT_4_3,           ///< 725x544, the arcade monitor's shape (default)
    VITA_ASPECT_SQUARE_PIXELS, ///< 932x544, every game pixel is square
    VITA_ASPECT_STRETCH,       ///< 960x544, fills the screen
} VitaAspect;

/// How the picture is scaled
typedef enum VitaFilter {
    VITA_FILTER_SHARP,   ///< Draw at 2x into an offscreen buffer, then scale it smoothly (default)
    VITA_FILTER_NEAREST, ///< Draw straight to the screen with nearest-neighbour sampling
} VitaFilter;

bool VitaRenderer_Init(VitaAspect aspect, VitaFilter filter);
void VitaRenderer_Shutdown();
void VitaRenderer_BeginFrame();
void VitaRenderer_RenderFrame();
void VitaRenderer_EndFrame();

/// Change how the picture is fitted to the screen. Takes effect on the next frame
void VitaRenderer_SetAspect(VitaAspect aspect);
VitaAspect VitaRenderer_GetAspect();

/// Show a plain text screen (for errors that happen before the game can start).
/// Lines are separated by '\n'.
void VitaRenderer_ShowMessage(const char* text);

void VitaRenderer_CreateTexture(unsigned int th);
void VitaRenderer_DestroyTexture(unsigned int texture_handle);
void VitaRenderer_UnlockTexture(unsigned int th);
void VitaRenderer_CreatePalette(unsigned int ph);
void VitaRenderer_DestroyPalette(unsigned int palette_handle);
void VitaRenderer_UnlockPalette(unsigned int ph);
void VitaRenderer_SetTexture(unsigned int th);
void VitaRenderer_DrawTexturedQuad(const Sprite* sprite, unsigned int color);
void VitaRenderer_DrawSprite(const Sprite* sprite, unsigned int color);
void VitaRenderer_DrawSprite2(const Sprite2* sprite2);
void VitaRenderer_DrawSolidQuad(const Quad* quad, unsigned int color);

#endif
