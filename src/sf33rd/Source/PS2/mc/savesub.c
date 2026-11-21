#include "sf33rd/Source/PS2/mc/savesub.h"
#include "common.h"
#include "main.h"
#include "port/paths.h"
#include "port/utils.h"
#include "sf33rd/Source/Game/sound/sound3rd.h"
#include "sf33rd/Source/Game/system/sys_sub.h"
#include "sf33rd/Source/Game/system/sys_sub2.h"
#include "sf33rd/Source/Game/system/work_sys.h"

#include <SDL3/SDL.h>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

#define SETTINGS_VERSION_V1 1
#define SETTINGS_VERSION_V2 2
#define SETTINGS_VERSION SETTINGS_VERSION_V2
#define SETTINGS_SIZE_V1 367
#define SETTINGS_SIZE_V2 368
#define SETTINGS_SIZE SETTINGS_SIZE_V2

#define SYSDIR_VERSION 1
#define SYSDIR_SIZE_V1 71
#define SYSDIR_SIZE SYSDIR_SIZE_V1

#define REPLAY_VERSION 1
#define REPLAY_SIZE_V1 0
#define REPLAY_SIZE REPLAY_SIZE_V1

#define ROOT_DIR "saves"
#define PATH_LEN_MAX 128

typedef enum SaveState {
    SAVE_STATE_IDLE,
    SAVE_STATE_INIT,
    SAVE_STATE_WORKING,
    SAVE_STATE_ERROR,
} SaveState;

typedef struct SaveOperation {
    SaveState state;
    SaveFileType file_type;
    SaveMode mode;
} SaveOperation;

typedef enum ReadResult {
    READ_SUCCESS,
    READ_NO_FILE,
    READ_SIZE_MISMATCH,
    READ_ERROR,
} ReadResult;

typedef void (*SerializeHandler)(SDL_IOStream* io);
typedef bool (*DeserializeHandler)(SDL_IOStream* io);

typedef struct SaveFileFormat {
    Uint8 version;
    Uint64 size;
} SaveFileFormat;

typedef struct SaveFileInfo {
    const char* name;
    const SaveFileFormat* formats;
    int format_count;
    SerializeHandler serialize_handler;
    DeserializeHandler deserialize_handler;
} SaveFileInfo;

static SaveOperation operation = { 0 };

// File access for the save files.
//
// Desktop builds go through SDL_Storage. On the Vita the files are read and written with the
// system's own sceIo calls instead: options saved through SDL_Storage did not come back after
// the app was closed and opened again, so the Vita no longer depends on that path.

typedef enum StoragePathType {
    STORAGE_PATH_NONE,
    STORAGE_PATH_FILE,
    STORAGE_PATH_DIRECTORY,
    STORAGE_PATH_OTHER,
} StoragePathType;

#ifdef __vita__

static void storage_full_path(char* text, size_t maxlen, const char* path) {
    SDL_snprintf(text, maxlen, "%s%s", Paths_GetPrefPath(), path);
}

static bool storage_open() {
    return true;
}

static bool storage_ready() {
    return true;
}

static void storage_close() {}

static StoragePathType storage_path_type(const char* path, Uint64* size) {
    char full_path[PATH_LEN_MAX];
    SceIoStat stat;
    storage_full_path(full_path, sizeof(full_path), path);
    SDL_zero(stat);

    if (sceIoGetstat(full_path, &stat) < 0) {
        return STORAGE_PATH_NONE;
    }

    if (size != NULL) {
        *size = (Uint64)stat.st_size;
    }

    if (SCE_S_ISREG(stat.st_mode)) {
        return STORAGE_PATH_FILE;
    }

    if (SCE_S_ISDIR(stat.st_mode)) {
        return STORAGE_PATH_DIRECTORY;
    }

    return STORAGE_PATH_OTHER;
}

static bool storage_read(const char* path, void* buffer, Uint64 length) {
    char full_path[PATH_LEN_MAX];
    storage_full_path(full_path, sizeof(full_path), path);
    const SceUID fd = sceIoOpen(full_path, SCE_O_RDONLY, 0);

    if (fd < 0) {
        SDL_Log("save: can't open %s for reading (0x%08X)", full_path, (unsigned int)fd);
        return false;
    }

    Uint64 done = 0;

    while (done < length) {
        const int read = sceIoRead(fd, (Uint8*)buffer + done, (SceSize)(length - done));

        if (read <= 0) {
            break;
        }

        done += read;
    }

    sceIoClose(fd);

    if (done != length) {
        SDL_Log("save: read %llu of %llu bytes from %s", (unsigned long long)done, (unsigned long long)length, full_path);
        return false;
    }

    return true;
}

static bool storage_write(const char* path, const void* buffer, Uint64 length) {
    char full_path[PATH_LEN_MAX];
    storage_full_path(full_path, sizeof(full_path), path);
    const SceUID fd = sceIoOpen(full_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);

    if (fd < 0) {
        SDL_Log("save: can't open %s for writing (0x%08X)", full_path, (unsigned int)fd);
        return false;
    }

    Uint64 done = 0;

    while (done < length) {
        const int written = sceIoWrite(fd, (const Uint8*)buffer + done, (SceSize)(length - done));

        if (written <= 0) {
            break;
        }

        done += written;
    }

    const int close_result = sceIoClose(fd);

    if (done != length || close_result < 0) {
        SDL_Log("save: wrote %llu of %llu bytes to %s (close 0x%08X)",
                (unsigned long long)done,
                (unsigned long long)length,
                full_path,
                (unsigned int)close_result);
        return false;
    }

    return true;
}

static bool storage_copy(const char* src, const char* dst) {
    Uint64 size = 0;

    if (storage_path_type(src, &size) != STORAGE_PATH_FILE) {
        return false;
    }

    void* buffer = SDL_malloc(size > 0 ? size : 1);

    if (buffer == NULL) {
        return false;
    }

    const bool success = storage_read(src, buffer, size) && storage_write(dst, buffer, size);
    SDL_free(buffer);
    return success;
}

static bool storage_create_directory(const char* path) {
    char full_path[PATH_LEN_MAX];
    storage_full_path(full_path, sizeof(full_path), path);
    const int result = sceIoMkdir(full_path, 0777);

    if (result < 0 && storage_path_type(path, NULL) != STORAGE_PATH_DIRECTORY) {
        SDL_Log("save: can't create %s (0x%08X)", full_path, (unsigned int)result);
        return false;
    }

    return true;
}

#else

static SDL_Storage* storage = NULL;

static bool storage_open() {
    storage = Paths_OpenUserStorage(0);
    return storage != NULL;
}

static bool storage_ready() {
    return SDL_StorageReady(storage);
}

static void storage_close() {
    SDL_CloseStorage(storage);
    storage = NULL;
}

static StoragePathType storage_path_type(const char* path, Uint64* size) {
    SDL_PathInfo info;

    if (!SDL_GetStoragePathInfo(storage, path, &info)) {
        return STORAGE_PATH_NONE;
    }

    if (size != NULL) {
        *size = info.size;
    }

    switch (info.type) {
    case SDL_PATHTYPE_FILE:
        return STORAGE_PATH_FILE;

    case SDL_PATHTYPE_DIRECTORY:
        return STORAGE_PATH_DIRECTORY;

    case SDL_PATHTYPE_NONE:
        return STORAGE_PATH_NONE;

    default:
        return STORAGE_PATH_OTHER;
    }
}

static bool storage_read(const char* path, void* buffer, Uint64 length) {
    return SDL_ReadStorageFile(storage, path, buffer, length);
}

static bool storage_write(const char* path, const void* buffer, Uint64 length) {
    return SDL_WriteStorageFile(storage, path, buffer, length);
}

static bool storage_copy(const char* src, const char* dst) {
    return SDL_CopyStorageFile(storage, src, dst);
}

static bool storage_create_directory(const char* path) {
    return SDL_CreateStorageDirectory(storage, path);
}

#endif

static void serialize_settings(SDL_IOStream* io) {
    Save_Game_Data();

    const struct _SAVE_W* src = &save_w[1];
    SDL_WriteU8(io, SETTINGS_VERSION);

    for (int i = 0; i < SDL_arraysize(src->Pad_Infor); i++) {
        const _PAD_INFOR* pad_info = &src->Pad_Infor[i];

        for (int j = 0; j < SDL_arraysize(pad_info->Shot); j++) {
            SDL_WriteU8(io, pad_info->Shot[j]);
        }

        SDL_WriteU8(io, pad_info->Vibration);
    }

    SDL_WriteU8(io, src->Difficulty);
    SDL_WriteS8(io, src->Time_Limit);
    SDL_WriteU8(io, src->Battle_Number[0]);
    SDL_WriteU8(io, src->Battle_Number[1]);
    SDL_WriteU8(io, src->Damage_Level);
    SDL_WriteU8(io, src->Handicap);
    SDL_WriteU8(io, src->Partner_Type[0]);
    SDL_WriteU8(io, src->Partner_Type[1]);
    SDL_WriteS8(io, src->Adjust_X);
    SDL_WriteS8(io, src->Adjust_Y);
    SDL_WriteU8(io, src->Screen_Size);
    SDL_WriteU8(io, src->Language);
    SDL_WriteU8(io, src->GuardCheck);
    SDL_WriteU8(io, src->AnalogStick);
    SDL_WriteU8(io, src->BgmType);
    SDL_WriteU8(io, src->BGM_Level);
    SDL_WriteU8(io, src->SE_Level);
    SDL_WriteIO(io, &src->extra_option, sizeof(src->extra_option));

    for (int i = 0; i < SDL_arraysize(src->Ranking); i++) {
        const RANK_DATA* rank_data = &src->Ranking[i];

        SDL_WriteIO(io, rank_data->name, sizeof(rank_data->name));
        SDL_WriteU16BE(io, rank_data->player);
        SDL_WriteU32BE(io, rank_data->score);
        SDL_WriteS8(io, rank_data->cpu_grade);
        SDL_WriteS8(io, rank_data->grade);
        SDL_WriteU16BE(io, rank_data->wins);
        SDL_WriteU8(io, rank_data->player_color);
        SDL_WriteU8(io, rank_data->all_clear);
    }
}

/// Put the saved Screen Adjust position and size back on screen. The game keeps them in the settings file but only
/// applied them while the Screen Adjust page was open, so they were lost at every start
static void apply_screen_adjust(const struct _SAVE_W* src) {
    u8 size_h;
    u8 size_v;

    X_Adjust = SDL_clamp(src->Adjust_X, -10, 10);
    Y_Adjust = SDL_clamp(src->Adjust_Y, -10, 10);
    X_Adjust_Buff[0] = X_Adjust_Buff[1] = X_Adjust_Buff[2] = X_Adjust;
    Y_Adjust_Buff[0] = Y_Adjust_Buff[1] = Y_Adjust_Buff[2] = Y_Adjust;
    dspwhUnpack(src->Screen_Size, &size_h, &size_v);
    Disp_Size_H = SDL_clamp(size_h, 94, 100);
    Disp_Size_V = SDL_clamp(size_v, 94, 100);
}

static bool deserialize_settings(SDL_IOStream* io) {
    struct _SAVE_W* dst = &save_w[1];
    Uint8 version;

    SDL_ReadU8(io, &version);

    if (version != SETTINGS_VERSION_V1 && version != SETTINGS_VERSION_V2) {
        return false;
    }

    for (int i = 0; i < SDL_arraysize(dst->Pad_Infor); i++) {
        _PAD_INFOR* pad_info = &dst->Pad_Infor[i];

        for (int j = 0; j < SDL_arraysize(pad_info->Shot); j++) {
            SDL_ReadU8(io, &pad_info->Shot[j]);
        }

        SDL_ReadU8(io, &pad_info->Vibration);
    }

    SDL_ReadU8(io, &dst->Difficulty);
    SDL_ReadS8(io, &dst->Time_Limit);
    SDL_ReadU8(io, &dst->Battle_Number[0]);
    SDL_ReadU8(io, &dst->Battle_Number[1]);
    SDL_ReadU8(io, &dst->Damage_Level);
    SDL_ReadU8(io, &dst->Handicap);
    SDL_ReadU8(io, &dst->Partner_Type[0]);
    SDL_ReadU8(io, &dst->Partner_Type[1]);
    SDL_ReadS8(io, &dst->Adjust_X);
    SDL_ReadS8(io, &dst->Adjust_Y);
    SDL_ReadU8(io, &dst->Screen_Size);

    if (version == SETTINGS_VERSION_V2) {
        SDL_ReadU8(io, &dst->Language);
    } else {
        dst->Language = Get_Default_Language();
    }

    SDL_ReadU8(io, &dst->GuardCheck);
    SDL_ReadU8(io, &dst->AnalogStick);
    SDL_ReadU8(io, &dst->BgmType);
    SDL_ReadU8(io, &dst->BGM_Level);
    SDL_ReadU8(io, &dst->SE_Level);
    SDL_ReadIO(io, &dst->extra_option, sizeof(dst->extra_option));

    for (int i = 0; i < SDL_arraysize(dst->Ranking); i++) {
        RANK_DATA* rank_data = &dst->Ranking[i];

        SDL_ReadIO(io, rank_data->name, sizeof(rank_data->name));
        SDL_ReadU16BE(io, &rank_data->player);
        SDL_ReadU32BE(io, &rank_data->score);
        SDL_ReadS8(io, &rank_data->cpu_grade);
        SDL_ReadS8(io, &rank_data->grade);
        SDL_ReadU16BE(io, &rank_data->wins);
        SDL_ReadU8(io, &rank_data->player_color);
        SDL_ReadU8(io, &rank_data->all_clear);
    }

    Copy_Save_w();
    Copy_Check_w();
    apply_screen_adjust(dst);
    sys_w.bgm_type = save_w[1].BgmType;
    bgm_level = save_w[1].BGM_Level;
    se_level = save_w[1].SE_Level;
    setSeVolume();
    return true;
}

static void serialize_sysdir(SDL_IOStream* io) {
    const SystemDir* src = &system_dir[1];

    SDL_WriteU8(io, SYSDIR_VERSION);

    for (int page = 0; page < SDL_arraysize(src->contents); page++) {
        for (int item = 0; item < SDL_arraysize(src->contents[page]); item++) {
            SDL_WriteS8(io, src->contents[page][item]);
        }
    }
}

static bool deserialize_sysdir(SDL_IOStream* io) {
    SystemDir loaded = { 0 };
    Uint8 version;

    SDL_ReadU8(io, &version);

    if (version != SYSDIR_VERSION) {
        return false;
    }

    for (int page = 0; page < SDL_arraysize(loaded.contents); page++) {
        for (int item = 0; item < SDL_arraysize(loaded.contents[page]); item++) {
            SDL_ReadS8(io, &loaded.contents[page][item]);
        }
    }

    system_dir[1] = loaded;
    return true;
}

static void serialize_stub(SDL_IOStream* io) {
    fatal_error("Not implemented");
}

static bool deserialize_stub(SDL_IOStream* io) {
    fatal_error("Not implemented");
}

static const SaveFileFormat settings_formats[] = {
    { SETTINGS_VERSION_V1, SETTINGS_SIZE_V1 },
    { SETTINGS_VERSION_V2, SETTINGS_SIZE_V2 },
};

static const SaveFileFormat sysdir_formats[] = {
    { SYSDIR_VERSION, SYSDIR_SIZE },
};

static const SaveFileFormat replay_formats[] = {
    { REPLAY_VERSION, REPLAY_SIZE },
};

static const SaveFileInfo file_info[] = {
    [SAVE_FILE_SETTINGS] = { .name = "settings",
                             .formats = settings_formats,
                             .format_count = SDL_arraysize(settings_formats),
                             .serialize_handler = serialize_settings,
                             .deserialize_handler = deserialize_settings },
    [SAVE_FILE_SYSTEM_DIRECTION] = { .name = "sysdir",
                                     .formats = sysdir_formats,
                                     .format_count = SDL_arraysize(sysdir_formats),
                                     .serialize_handler = serialize_sysdir,
                                     .deserialize_handler = deserialize_sysdir },
    [SAVE_FILE_REPLAY] = { .name = "replay",
                           .formats = replay_formats,
                           .format_count = SDL_arraysize(replay_formats),
                           .serialize_handler = serialize_stub,
                           .deserialize_handler = deserialize_stub },
};

static void make_path(char* text, size_t maxlen, const char* name, bool backup) {
    SDL_snprintf(text, maxlen, backup ? "%s/%s.bak" : "%s/%s", ROOT_DIR, name);
}

static const SaveFileFormat* current_format(const SaveFileInfo* file) {
    return &file->formats[file->format_count - 1];
}

static ReadResult read_file_if_exists(const char* path, void* buffer, const SaveFileInfo* file) {
    const SaveFileFormat* format = NULL;
    Uint64 size = 0;

    switch (storage_path_type(path, &size)) {
    case STORAGE_PATH_FILE:
        break;

    case STORAGE_PATH_NONE:
        return READ_NO_FILE;

    default:
        return READ_ERROR;
    }

    for (int i = 0; i < file->format_count; i++) {
        if (size == file->formats[i].size) {
            format = &file->formats[i];
            break;
        }
    }

    if (format == NULL) {
        SDL_Log("save: %s has an unexpected size (%llu bytes)", path, (unsigned long long)size);
        return READ_SIZE_MISMATCH;
    }

    if (!storage_read(path, buffer, format->size)) {
        return READ_ERROR;
    }

    if (*(const Uint8*)buffer != format->version) {
        return READ_ERROR;
    }

    return READ_SUCCESS;
}

static bool create_saves_dir() {
    if (storage_path_type(ROOT_DIR, NULL) == STORAGE_PATH_DIRECTORY) {
        return true;
    }

    return storage_create_directory(ROOT_DIR);
}

void SaveInit(SaveFileType file_type, SaveMode save_mode) {
    operation.state = SAVE_STATE_INIT;
    operation.file_type = file_type;
    operation.mode = save_mode;
}

s32 SaveMove() {
    switch (operation.state) {
    case SAVE_STATE_IDLE:
        return 0;

    case SAVE_STATE_INIT:
        if (!storage_open()) {
            operation.state = SAVE_STATE_ERROR;
            return -1;
        }

        operation.state = SAVE_STATE_WORKING;
        /* fallthrough */

    case SAVE_STATE_WORKING:
        if (!storage_ready()) {
            return 1;
        }

        const SaveFileInfo* info = &file_info[operation.file_type];
        char path[PATH_LEN_MAX];
        char backup_path[PATH_LEN_MAX];
        make_path(path, sizeof(path), info->name, false);
        make_path(backup_path, sizeof(backup_path), info->name, true);
        const SaveFileFormat* format = current_format(info);
        void* buffer = SDL_malloc(format->size);
        SDL_IOStream* io = NULL;
        bool success = false;

        switch (operation.mode) {
        case SAVE_MODE_LOAD:
            io = SDL_IOFromConstMem(buffer, format->size);
            bool buffer_filled = false;
            const char* paths[] = { path, backup_path };

            for (int i = 0; i < SDL_arraysize(paths); i++) {
                const char* _path = paths[i];
                SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);

                const ReadResult read_result = read_file_if_exists(_path, buffer, info);

                if (read_result == READ_SUCCESS) {
                    success = info->deserialize_handler(io);
                }

                if (success) {
                    SDL_Log("save: loaded %s", _path);
                } else if (read_result != READ_NO_FILE) {
                    SDL_Log("save: couldn't load %s", _path);
                }

                if (success) {
                    break;
                }
            }

            break;

        case SAVE_MODE_SAVE:
            io = SDL_IOFromMem(buffer, format->size);
            info->serialize_handler(io);

            const bool saves_dir_exists = create_saves_dir();

            if (saves_dir_exists) {
                bool backup_success = false;
                const bool save_file_exists = storage_path_type(path, NULL) == STORAGE_PATH_FILE;

                if (save_file_exists) {
                    backup_success = storage_copy(path, backup_path);
                } else {
                    backup_success = true; // Nothing to backup
                }

                if (backup_success) {
                    success = storage_write(path, buffer, format->size);
                }
            }

            SDL_Log(success ? "save: saved %s" : "save: couldn't save %s", path);
            break;
        }

        SDL_CloseIO(io);
        SDL_free(buffer);
        storage_close();
        SDL_zero(operation);
        return success ? 0 : -1;

    case SAVE_STATE_ERROR:
        return -1;
    }

    return -1;
}
