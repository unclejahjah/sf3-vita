#include "port/paths.h"

#include <SDL3/SDL.h>

#ifdef __vita__
#include <psp2/io/stat.h>
#endif

#define ORG "CrowdedStreet"
#define APP "3SX"

#if CRS_APP_DRIVER_VITA
// Keep everything in one short folder that is easy to reach over FTP or USB
#define VITA_PREF_PATH "ux0:data/3sx/"
#define VITA_BASE_PATH "app0:"
#endif

static const char* pref_path = NULL;

const char* Paths_GetPrefPath() {
    if (pref_path == NULL) {
#if CRS_APP_DRIVER_VITA
        pref_path = VITA_PREF_PATH;
#ifdef __vita__
        sceIoMkdir("ux0:data", 0777);
        sceIoMkdir("ux0:data/3sx", 0777);
        sceIoMkdir("ux0:data/3sx/resources", 0777);
#else
        SDL_CreateDirectory(VITA_PREF_PATH "resources");
#endif
#else
        pref_path = SDL_GetPrefPath(ORG, APP);
#endif
    }

    return pref_path;
}

char* Paths_GetCachePath(const char* file_path) {
    const char* base = Paths_GetPrefPath();
    char* full_path = NULL;

    if (file_path == NULL) {
        SDL_asprintf(&full_path, "%scache/", base);
    } else {
        SDL_asprintf(&full_path, "%scache/%s", base, file_path);
    }

    return full_path;
}

const char* Paths_GetBasePath() {
#if CRS_APP_DRIVER_VITA
    return VITA_BASE_PATH;
#else
    return SDL_GetBasePath();
#endif
}

SDL_Storage* Paths_OpenUserStorage(SDL_PropertiesID props) {
#if CRS_APP_DRIVER_VITA
    return SDL_OpenFileStorage(VITA_PREF_PATH);
#else
    return SDL_OpenUserStorage(ORG, APP, props);
#endif
}
