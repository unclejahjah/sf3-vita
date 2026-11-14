#ifndef VITA_APP_H
#define VITA_APP_H

void VitaApp_Exit();

/// Run once per frame while the game's Screen Adjust page is open.
/// SELECT changes how the picture is fitted to the screen, and the choice is saved to the config file
void VitaApp_ScreenAdjustTick();

#endif
