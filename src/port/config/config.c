#include "port/config/config.h"
#include "port/config/config_helpers.h"
#include "port/paths.h"

#include <stdbool.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#define CONFIG_ENTRIES_MAX 128

typedef enum ConfigType {
    CFG_BOOL,
    CFG_INT,
    CFG_STRING,
} ConfigType;

typedef union ConfigValue {
    bool b;
    int i;
    char* s;
} ConfigValue;

typedef struct ConfigEntry {
    const char* key;
    ConfigType type;
    ConfigValue value;
} ConfigEntry;

static const ConfigEntry default_entries[] = {
    { .key = CFG_KEY_FULLSCREEN, .type = CFG_BOOL, .value.b = true },
    { .key = CFG_KEY_WINDOW_WIDTH, .type = CFG_INT, .value.i = 640 },
    { .key = CFG_KEY_WINDOW_HEIGHT, .type = CFG_INT, .value.i = 480 },
    { .key = CFG_KEY_SCALEMODE, .type = CFG_STRING, .value.s = "nearest" },
    { .key = CFG_KEY_SCANLINES, .type = CFG_INT, .value.i = 0 },
    { .key = CFG_DRAW_PLAYERS_ABOVE_HUD, .type = CFG_BOOL, .value.b = false },
    { .key = CFG_ARCADE_BALANCE, .type = CFG_BOOL, .value.b = false },
#if CRS_APP_DRIVER_VITA
    { .key = CFG_KEY_VITA_ASPECT, .type = CFG_STRING, .value.s = "4:3" },
    { .key = CFG_KEY_VITA_FILTER, .type = CFG_STRING, .value.s = "sharp" },
    { .key = CFG_KEY_VITA_PERF_LOG, .type = CFG_BOOL, .value.b = true },
#endif
};

static ConfigEntry entries[CONFIG_ENTRIES_MAX] = { 0 };
static int entry_count = 0;

static bool is_int(const char* string) {
    for (int i = 0; i < SDL_strlen(string); i++) {
        if (SDL_isdigit(string[i]) || ((i == 0) && (string[i] == '-'))) {
            continue;
        } else {
            return false;
        }
    }

    return true;
}

static const ConfigEntry* find_entry_in_array(const char* key, const ConfigEntry* array, size_t size) {
    for (int i = 0; i < size; i++) {
        const ConfigEntry* entry = &array[i];

        if (SDL_strcmp(key, entry->key) == 0) {
            return entry;
        }
    }

    return NULL;
}

static const ConfigEntry* find_entry(const char* key) {
    const ConfigEntry* default_entry = find_entry_in_array(key, default_entries, SDL_arraysize(default_entries));
    const ConfigEntry* read_entry = find_entry_in_array(key, entries, entry_count);

    if (read_entry != NULL) {
        if (default_entry != NULL && read_entry->type != default_entry->type) {
            // If we expect a certain type and the one we read from config is unexpected, let's use the default entry
            // instead
            return default_entry;
        } else {
            return read_entry;
        }
    } else if (default_entry != NULL) {
        return default_entry;
    } else {
        SDL_assert(false);
        return NULL;
    }
}

static void print_config_entry_to_io(SDL_IOStream* io, const ConfigEntry* entry) {
    io_printf(io, "%s = ", entry->key);

    switch (entry->type) {
    case CFG_BOOL:
        io_printf(io, entry->value.b ? "true" : "false");
        break;

    case CFG_INT:
        io_printf(io, "%d", entry->value.i);
        break;

    case CFG_STRING:
        io_printf(io, entry->value.s);
        break;
    }
}

/// Write every known setting with its current value, then the settings from the file that this build doesn't know
static void write_config(const char* dst_path) {
    SDL_IOStream* io = SDL_IOFromFile(dst_path, "w");

    if (io == NULL) {
        return;
    }

    io_printf(io,
              "# For the full list of settings see https://github.com/crowded-street/3sx/blob/main/docs/config.md\n\n");

    for (int i = 0; i < SDL_arraysize(default_entries); i++) {
        print_config_entry_to_io(io, find_entry(default_entries[i].key));
        io_printf(io, "\n");
    }

    for (int i = 0; i < entry_count; i++) {
        const ConfigEntry* entry = &entries[i];

        if (find_entry_in_array(entry->key, default_entries, SDL_arraysize(default_entries)) == NULL) {
            print_config_entry_to_io(io, entry);
            io_printf(io, "\n");
        }
    }

    SDL_CloseIO(io);
}

static char* get_config_path() {
    char* config_path = NULL;
    SDL_asprintf(&config_path, "%sconfig", Paths_GetPrefPath());
    return config_path;
}

static bool dict_iterator(const char* key, const char* value) {
    if (entry_count == CONFIG_ENTRIES_MAX) {
        printf("⚠️ Reached max config entry count (%d), skipping the rest\n", CONFIG_ENTRIES_MAX);
        return false;
    }

    ConfigEntry* entry = &entries[entry_count];
    entry->key = SDL_strdup(key);

    const bool is_true = SDL_strcmp(value, "true") == 0;
    const bool is_false = SDL_strcmp(value, "false") == 0;

    if (is_true || is_false) {
        entry->type = CFG_BOOL;
        entry->value.b = is_true;
    } else if (is_int(value)) {
        entry->type = CFG_INT;
        entry->value.i = SDL_atoi(value);
    } else {
        entry->type = CFG_STRING;
        entry->value.s = SDL_strdup(value);
    }

    entry_count += 1;
    return true;
}

void Config_Init() {
    char* config_path = get_config_path();
    FILE* f = fopen(config_path, "r");

    if (f == NULL) {
        // Config doesn't exist. Write defaults
        write_config(config_path);
        SDL_free(config_path);
        return;
    }

    SDL_free(config_path);
    dict_read(f, dict_iterator);
    fclose(f);
}

void Config_SetString(const char* key, const char* value) {
    for (int i = 0; i < entry_count; i++) {
        ConfigEntry* entry = &entries[i];

        if (SDL_strcmp(entry->key, key) != 0) {
            continue;
        }

        if (entry->type == CFG_STRING) {
            SDL_free(entry->value.s);
        }

        entry->type = CFG_STRING;
        entry->value.s = SDL_strdup(value);
        return;
    }

    if (entry_count == CONFIG_ENTRIES_MAX) {
        return;
    }

    ConfigEntry* entry = &entries[entry_count];
    entry->key = SDL_strdup(key);
    entry->type = CFG_STRING;
    entry->value.s = SDL_strdup(value);
    entry_count += 1;
}

void Config_Save() {
    char* config_path = get_config_path();
    write_config(config_path);
    SDL_free(config_path);
}

void Config_Destroy() {
    for (int i = 0; i < entry_count; i++) {
        ConfigEntry* entry = &entries[i];
        SDL_free((void*)entry->key);

        if (entry->type == CFG_STRING) {
            SDL_free(entry->value.s);
        }
    }

    SDL_zeroa(entries);
    entry_count = 0;
}

bool Config_GetBool(const char* key) {
    const ConfigEntry* entry = find_entry(key);

    if (entry == NULL || entry->type != CFG_BOOL) {
        return false;
    }

    return entry->value.b;
}

int Config_GetInt(const char* key) {
    const ConfigEntry* entry = find_entry(key);

    if (entry == NULL || entry->type != CFG_INT) {
        return 0;
    }

    return entry->value.i;
}

const char* Config_GetString(const char* key) {
    const ConfigEntry* entry = find_entry(key);

    if (entry == NULL || entry->type != CFG_STRING) {
        return NULL;
    }

    return entry->value.s;
}
