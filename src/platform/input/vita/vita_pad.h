#if CRS_INPUT_DRIVER_VITA

#ifndef VITA_PAD_H
#define VITA_PAD_H

#include "core/input.h"

#include <stdbool.h>

void VitaPad_Init();
/// Read the controllers. Call once per frame, right before the game runs
void VitaPad_Update();

bool VitaPad_IsGamepadConnected(int id);
void VitaPad_GetButtonState(int id, Input_ButtonState* state);
void VitaPad_RumblePad(int id, bool low_freq_enabled, Uint8 high_freq_rumble);

#endif

#endif // CRS_INPUT_DRIVER_VITA
