/**
 * @brief PS Vita application driver.
 *
 * Owns the main loop. Drawing goes through the vita2d video driver, buttons and sound through SDL.
 * Everything the game reads or writes lives in ux0:data/3sx/ (see port/paths.c).
 */

#if CRS_APP_DRIVER_VITA

#include "platform/app/vita/vita_app.h"

#include "core/input.h"
#include "main.h"
#include "platform/input/sdl/sdl_pad.h"
#include "platform/video/vita/vita_renderer.h"
#include "port/config/config.h"
#include "port/io/resource.h"
#include "port/paths.h"
#include "port/resources.h"
#include "port/sound/adx.h"

// This file has a plain main(), SDL doesn't need to wrap it
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#endif

#include <stdbool.h>
#include <stdio.h>

#define LOG_FILE_NAME "log.txt"

/// How much of the archive is read per frame. A memory card manages this without dropping frames
#define READ_CHUNK_SIZE (64 * 1024)

#ifdef __vita__
// The C library's heap is a fixed block. The game needs about 80 MB with music loaded
int _newlib_heap_size_user = 160 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;
#endif

static char* log_path = NULL;

/// The file is opened and closed for every line. A line that is only flushed can be lost when the game crashes
static void log_to_file(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    if (log_path == NULL) {
        return;
    }

    FILE* file = fopen(log_path, "a");

    if (file == NULL) {
        return;
    }

    fprintf(file, "%s\n", message);
    fclose(file);
}

static void init_log() {
    SDL_asprintf(&log_path, "%s%s", Paths_GetPrefPath(), LOG_FILE_NAME);

    // Start a new log on every run
    FILE* file = fopen(log_path, "w");

    if (file != NULL) {
        fclose(file);
    }

    SDL_SetLogOutputFunction(log_to_file, NULL);
}

static void init_power() {
#ifdef __vita__
    // Run the CPU and GPU at full speed
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
#endif
}

static VitaAspect read_aspect() {
    const char* value = Config_GetString(CFG_KEY_VITA_ASPECT);

    if (value == NULL) {
        return VITA_ASPECT_4_3;
    }

    if (SDL_strcmp(value, "square-pixels") == 0) {
        return VITA_ASPECT_SQUARE_PIXELS;
    }

    if (SDL_strcmp(value, "stretch") == 0) {
        return VITA_ASPECT_STRETCH;
    }

    return VITA_ASPECT_4_3;
}

static VitaFilter read_filter() {
    const char* value = Config_GetString(CFG_KEY_VITA_FILTER);

    if (value != NULL && SDL_strcmp(value, "nearest") == 0) {
        return VITA_FILTER_NEAREST;
    }

    return VITA_FILTER_SHARP;
}

static bool poll_sdl_events() {
    SDL_Event event;
    bool continue_running = true;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            SDLPad_HandleGamepadDeviceEvent(&event.gdevice);
            break;

        case SDL_EVENT_QUIT:
            continue_running = false;
            break;
        }
    }

    return continue_running;
}

/// Keep a message on the screen until START is pressed
static void show_message_until_start(const char* text) {
    while (poll_sdl_events()) {
        Input_ButtonState state;
        Input_GetButtonState(0, &state);

        if (state.start) {
            break;
        }

        VitaRenderer_ShowMessage(text);
    }
}

static bool init() {
    init_power();
    init_log();

    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return false;
    }

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        SDL_Log("Couldn't initialize audio, continuing without sound: %s", SDL_GetError());
    }

    Config_Init();

    if (!VitaRenderer_Init(read_aspect(), read_filter())) {
        SDL_Log("Couldn't initialize the renderer");
        return false;
    }

    SDLPad_Init();

    if (!Resources_Check()) {
        char* message = NULL;
        SDL_asprintf(
            &message,
            "SF33RD.AFS is missing.\n"
            "\n"
            "3SX needs this file from your own copy of\n"
            "Street Fighter III: 3rd Strike (PS2).\n"
            "\n"
            "Copy it to:\n"
            "%s\n"
            "\n"
            "Press START to exit.",
            Resources_GetAFSPath()
        );
        SDL_Log("%s", message);
        show_message_until_start(message);
        SDL_free(message);
        return false;
    }

    if (!Resource_Init(Resources_GetAFSPath(), READ_CHUNK_SIZE)) {
        SDL_Log("Couldn't open %s", Resources_GetAFSPath());
        show_message_until_start("SF33RD.AFS could not be read.\n\nPress START to exit.");
        return false;
    }

    Main_Init();
    return true;
}

static void quit() {
    VitaRenderer_Shutdown();
    SDL_Quit();

#ifdef __vita__
    sceKernelExitProcess(0);
#endif
}

void VitaApp_Exit() {
    SDL_Event quit_event;
    SDL_zero(quit_event);
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
}

int main(int argc, char* argv[]) {
    SDL_SetMainReady();

    if (!init()) {
        quit();
        return 1;
    }

    while (poll_sdl_events()) {
#ifdef __vita__
        // Without this the console dims the screen and suspends while a demo plays
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
#endif
        VitaRenderer_BeginFrame();
        Resource_RunServer();
        Main_StepFrame();
        ADX_ProcessTracks();
        VitaRenderer_RenderFrame();
        // Waits for the display, which is what paces the game
        VitaRenderer_EndFrame();
        Main_FinishFrame();
    }

    Main_Finish();
    Resource_Finish();
    Config_Destroy();
    quit();
    return 0;
}

#endif // CRS_APP_DRIVER_VITA
