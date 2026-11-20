#ifndef VITA_CRT_H
#define VITA_CRT_H

#include <stdbool.h>

/// Light CRT looks for the Vita: scanlines and a phosphor mask, multiplied onto the picture in one pass with
/// vita2d's own texture shader. The pattern is computed on the CPU when the look, the intensity or the size of the
/// picture changes, so a frame costs one textured quad.
typedef enum VitaCrtLook {
    VITA_CRT_SCANLINES,   ///< Scanlines only
    VITA_CRT_SCAN_SOFT,   ///< Faint, smooth scanlines
    VITA_CRT_SCAN_HARD,   ///< Thick dark gaps between the lines, like a broadcast monitor
    VITA_CRT_APERTURE,    ///< Scanlines + aperture grille (vertical RGB stripes, like a Trinitron)
    VITA_CRT_SHADOW_MASK, ///< Scanlines + staggered RGB triads
    VITA_CRT_SLOT_MASK,   ///< Scanlines + RGB stripes broken into slots
} VitaCrtLook;

bool VitaCrt_Init();
void VitaCrt_Shutdown();

/// Multiply the look onto the picture already drawn in the rectangle x, y, w, h (screen pixels).
/// `intensity` is 0 to 100 percent. Call between vita2d_start_drawing and vita2d_end_drawing
void VitaCrt_Draw(VitaCrtLook look, int intensity, int x, int y, int w, int h);

#endif
