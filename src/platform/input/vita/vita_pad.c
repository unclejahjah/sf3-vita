/**
 * @brief Controller input for the PS Vita and PS TV.
 *
 * Player 1 is the console's own buttons on a Vita and the first controller on a PS TV.
 * Player 2 is the next paired controller. Controllers can be paired while the game runs.
 */

#if CRS_INPUT_DRIVER_VITA

#include "platform/input/vita/vita_pad.h"

#include <SDL3/SDL.h>
#include <psp2/ctrl.h>

#define PLAYERS_MAX 2
/// Ports 2 to 4 are the extra controllers. Port 1 is the same controller as port 0
#define FIRST_EXTRA_PORT 2
#define LAST_EXTRA_PORT 4
/// Frames between two looks at which controllers are paired
#define PORT_SCAN_INTERVAL 30
/// The furthest a stick reads from its centre
#define STICK_MAX 127
/// A trigger pressed further than this counts as held
#define TRIGGER_PRESSED 127

/// The port each player reads, or -1
static int ports[PLAYERS_MAX] = { 0, -1 };
static Input_ButtonState states[PLAYERS_MAX];
static int frames_until_scan = 0;

static void scan_ports() {
    SceCtrlPortInfo info;
    SDL_zero(info);

    // The first player is always there, like the buttons of a Vita
    ports[0] = 0;

    for (int i = 1; i < PLAYERS_MAX; i++) {
        ports[i] = -1;
    }

    if (sceCtrlGetControllerPortInfo(&info) < 0) {
        return;
    }

    int player = 1;

    for (int port = FIRST_EXTRA_PORT; (port <= LAST_EXTRA_PORT) && (player < PLAYERS_MAX); port++) {
        if (info.port[port] != SCE_CTRL_TYPE_UNPAIRED) {
            ports[player] = port;
            player += 1;
        }
    }
}

static Sint16 stick_value(unsigned char raw) {
    const int centred = (int)raw - 128;
    return (Sint16)SDL_clamp(centred * SDL_MAX_SINT16 / STICK_MAX, SDL_MIN_SINT16, SDL_MAX_SINT16);
}

static bool read_port(int port, SceCtrlData* pad) {
    SDL_zerop(pad);

    if (port == 0) {
        // Port 1 is the first controller of a PS TV. A Vita doesn't have it and answers on port 0
        if (sceCtrlPeekBufferPositive2(1, pad, 1) >= 0) {
            return true;
        }
    }

    return sceCtrlPeekBufferPositive2(port, pad, 1) >= 0;
}

static void read_player(int player) {
    Input_ButtonState* state = &states[player];
    SceCtrlData pad;

    SDL_zerop(state);

    if ((ports[player] < 0) || !read_port(ports[player], &pad)) {
        return;
    }

    const unsigned int buttons = pad.buttons;

    state->dpad_up = (buttons & SCE_CTRL_UP) != 0;
    state->dpad_down = (buttons & SCE_CTRL_DOWN) != 0;
    state->dpad_left = (buttons & SCE_CTRL_LEFT) != 0;
    state->dpad_right = (buttons & SCE_CTRL_RIGHT) != 0;
    state->north = (buttons & SCE_CTRL_TRIANGLE) != 0;
    state->west = (buttons & SCE_CTRL_SQUARE) != 0;
    state->south = (buttons & SCE_CTRL_CROSS) != 0;
    state->east = (buttons & SCE_CTRL_CIRCLE) != 0;
    state->left_shoulder = (buttons & SCE_CTRL_L1) != 0;
    state->right_shoulder = (buttons & SCE_CTRL_R1) != 0;
    // L2 and R2 only exist on the controllers of a PS TV, which report them as a button and as a pressure
    state->left_trigger = ((buttons & SCE_CTRL_L2) || (pad.lt > TRIGGER_PRESSED)) ? SDL_MAX_SINT16 : 0;
    state->right_trigger = ((buttons & SCE_CTRL_R2) || (pad.rt > TRIGGER_PRESSED)) ? SDL_MAX_SINT16 : 0;
    state->left_stick = (buttons & SCE_CTRL_L3) != 0;
    state->right_stick = (buttons & SCE_CTRL_R3) != 0;
    state->back = (buttons & SCE_CTRL_SELECT) != 0;
    state->start = (buttons & SCE_CTRL_START) != 0;

    state->left_stick_x = stick_value(pad.lx);
    state->left_stick_y = stick_value(pad.ly);
    state->right_stick_x = stick_value(pad.rx);
    state->right_stick_y = stick_value(pad.ry);

    if (state->dpad_up && state->dpad_down) {
        state->dpad_up = false;
        state->dpad_down = false;
    }

    if (state->dpad_left && state->dpad_right) {
        state->dpad_left = false;
        state->dpad_right = false;
    }
}

void VitaPad_Init() {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    scan_ports();
    frames_until_scan = PORT_SCAN_INTERVAL;
}

void VitaPad_Update() {
    frames_until_scan -= 1;

    if (frames_until_scan <= 0) {
        scan_ports();
        frames_until_scan = PORT_SCAN_INTERVAL;
    }

    for (int i = 0; i < PLAYERS_MAX; i++) {
        read_player(i);
    }
}

bool VitaPad_IsGamepadConnected(int id) {
    return (id >= 0) && (id < PLAYERS_MAX) && (ports[id] >= 0);
}

void VitaPad_GetButtonState(int id, Input_ButtonState* state) {
    if (state == NULL) {
        return;
    }

    if ((id < 0) || (id >= PLAYERS_MAX)) {
        SDL_zerop(state);
        return;
    }

    *state = states[id];
}

void VitaPad_RumblePad(int id, bool low_freq_enabled, Uint8 high_freq_rumble) {
    if (!VitaPad_IsGamepadConnected(id)) {
        return;
    }

    // Only the controllers of a PS TV have motors. The call does nothing on a Vita
    SceCtrlActuator actuator;
    actuator.small = high_freq_rumble > 0 ? 1 : 0;
    actuator.large = low_freq_enabled ? UINT8_MAX : 0;
    sceCtrlSetActuator((ports[id] == 0) ? 1 : ports[id], &actuator);
}

#endif // CRS_INPUT_DRIVER_VITA
