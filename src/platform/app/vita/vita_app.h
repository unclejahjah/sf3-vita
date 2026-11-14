#ifndef VITA_APP_H
#define VITA_APP_H

void VitaApp_Exit();

/// How the picture is fitted to the screen: 0 is 4:3, 1 is wide with square pixels, 2 fills the screen.
/// This is the SCREEN SHAPE row of the game's Screen Adjust page
int VitaApp_GetPictureShape();

/// Choose a picture shape and save it to the config file. Values past either end wrap around
void VitaApp_SetPictureShape(int shape);

#endif
