#ifndef VITA_APP_H
#define VITA_APP_H

void VitaApp_Exit();

/// How the picture is fitted to the screen: 0 is 4:3, 1 is wide with square pixels, 2 fills the screen.
/// This is the SCREEN SHAPE row of the game's Screen Adjust page
int VitaApp_GetPictureShape();

/// Choose a picture shape and save it to the config file. Values past either end wrap around
void VitaApp_SetPictureShape(int shape);

/// Post-processing filter (FILTER row of the Screen Adjust page): 0 is off, then the filters in the order of
/// `VitaRenderer_GetFilterId`. Values past either end wrap around. The choice is saved to the config file
int VitaApp_GetFilter();
void VitaApp_SetFilter(int filter);

/// How strongly the filter shows (INTENSITY row), in steps of `VITA_FILTER_INTENSITY_STEP` percent:
/// 0 is 0%, 10 is 100%. Values past either end are clamped. The choice is saved to the config file
#define VITA_FILTER_INTENSITY_STEP 10
int VitaApp_GetFilterIntensity();
void VitaApp_SetFilterIntensity(int step);

#endif
