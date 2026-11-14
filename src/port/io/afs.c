#include "port/io/afs.h"
#include "common.h"

#include "stb/stb_ds.h"
#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

#if CRS_APP_DRIVER_VITA
#include "platform/app/vita/vita_perf.h"
#endif

// Inspired by https://github.com/MaikelChan/AFSLib

#define AFS_MAGIC 0x41465300
#define AFS_ATTRIBUTE_HEADER_SIZE 8
#define AFS_ATTRIBUTE_ENTRY_SIZE 48
#define AFS_MAX_NAME_LENGTH 32

typedef struct AFSEntry {
    Uint32 offset;
    Uint32 size;
    char name[AFS_MAX_NAME_LENGTH];
} AFSEntry;

typedef struct AFS {
    const char* file_path;
    Uint32 entry_count;
    AFSEntry* entries;
} AFS;

typedef struct ReadRequest {
    bool initialized;
    size_t file_num;
    size_t bytes_to_read;
    size_t bytes_read;
    Uint8* buf;
    AFSReadState state;
} ReadRequest;

static AFS afs = { 0 };
static ReadRequest* requests = NULL;
static SDL_IOStream* stream = NULL;
static size_t _read_chunk_size = 0;

#ifdef __vita__
/// The archive is read with positioned kernel reads on the Vita.
/// Seeking in a C stream is not reliable there
static SceUID vita_fd = -1;
static int read_errors_logged = 0;

#define READ_ERRORS_LOGGED_MAX 8
#define READ_ATTEMPTS_MAX 3

static void vita_reopen() {
    if (vita_fd >= 0) {
        sceIoClose(vita_fd);
    }

    vita_fd = sceIoOpen(afs.file_path, SCE_O_RDONLY, 0);
}

static size_t vita_read_at(Sint64 offset, void* buf, size_t size) {
    size_t total = 0;
    int attempts = 0;

    while (total < size) {
        const int result = sceIoPread(vita_fd, (Uint8*)buf + total, size - total, offset + total);

        if (result > 0) {
            total += result;
            attempts = 0;
            continue;
        }

        if (read_errors_logged < READ_ERRORS_LOGGED_MAX) {
            SDL_Log(
                "[AFS] Read failed: result 0x%08X at offset 0x%llX, wanted 0x%X bytes, attempt %d",
                (unsigned int)result,
                (unsigned long long)(offset + total),
                (unsigned int)(size - total),
                attempts + 1
            );
            read_errors_logged += 1;
        }

        attempts += 1;

        if (attempts >= READ_ATTEMPTS_MAX) {
            break;
        }

        // The handle stops working when the console has been suspended. A new one works again
        vita_reopen();
    }

    return total;
}
#endif

static void _log(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);

    int len = SDL_snprintf(buffer, sizeof(buffer), "[AFS] ");
    SDL_vsnprintf(buffer + len, sizeof(buffer) - len, fmt, args);
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG, "%s", buffer);

    va_end(args);
}

static bool is_valid_attribute_data(
    Uint32 attributes_offset, Uint32 attributes_size, Sint64 file_size, Uint32 entries_end_offset, Uint32 entry_count
) {
    if ((attributes_offset == 0) || (attributes_size == 0)) {
        return false;
    }

    if (attributes_size > (file_size - entries_end_offset)) {
        return false;
    }

    if (attributes_size < (entry_count * AFS_ATTRIBUTE_ENTRY_SIZE)) {
        return false;
    }

    if (attributes_offset < entries_end_offset) {
        return false;
    }

    if (attributes_offset > (file_size - attributes_size)) {
        return false;
    }

    return true;
}

static void read_string(SDL_IOStream* src, char* dst) {
    Sint8 c;

    do {
        SDL_ReadS8(src, &c);
        *dst++ = c;
    } while (c != '\0');
}

static bool init_afs(const char* file_path) {
    afs.file_path = SDL_strdup(file_path);
    SDL_IOStream* io = SDL_IOFromFile(file_path, "rb");

    if (io == NULL) {
        return false;
    }

    // Check magic

    Uint32 magic = 0;
    SDL_ReadU32BE(io, &magic);

    if (magic != AFS_MAGIC) {
        SDL_CloseIO(io);
        return false;
    }

    // Read entries

    SDL_ReadU32LE(io, &afs.entry_count);
    afs.entries = SDL_calloc(afs.entry_count, sizeof(AFSEntry));

    Uint32 entries_start_offset = 0;
    Uint32 entries_end_offset = 0;

    for (int i = 0; i < afs.entry_count; i++) {
        AFSEntry* entry = &afs.entries[i];
        SDL_ReadU32LE(io, &entry->offset);
        SDL_ReadU32LE(io, &entry->size);

        if (entry->offset != 0) {
            if (entries_start_offset == 0) {
                entries_start_offset = entry->offset;
            }

            entries_end_offset = entry->offset + entry->size;
        }
    }

    // Locate attributes

    Uint32 attributes_offset;
    Uint32 attributes_size;
    bool has_attributes = false;

    SDL_ReadU32LE(io, &attributes_offset);
    SDL_ReadU32LE(io, &attributes_size);

    if (is_valid_attribute_data(
            attributes_offset, attributes_size, SDL_GetIOSize(io), entries_end_offset, afs.entry_count
        )) {
        has_attributes = true;
    } else {
        SDL_SeekIO(io, entries_start_offset - AFS_ATTRIBUTE_HEADER_SIZE, SDL_IO_SEEK_SET);

        SDL_ReadU32LE(io, &attributes_offset);
        SDL_ReadU32LE(io, &attributes_size);

        if (is_valid_attribute_data(
                attributes_offset, attributes_size, SDL_GetIOSize(io), entries_end_offset, afs.entry_count
            )) {
            has_attributes = true;
        }
    }

    for (int i = 0; i < afs.entry_count; i++) {
        AFSEntry* entry = &afs.entries[i];

        if ((entry->offset != 0) && has_attributes) {
            SDL_SeekIO(io, attributes_offset + i * AFS_ATTRIBUTE_ENTRY_SIZE, SDL_IO_SEEK_SET);
            read_string(io, entry->name);
        } else {
            SDL_zeroa(entry->name);
        }
    }

#ifdef __vita__
    vita_fd = sceIoOpen(file_path, SCE_O_RDONLY, 0);

    if (vita_fd < 0) {
        SDL_Log("[AFS] Couldn't open %s: 0x%08X", file_path, (unsigned int)vita_fd);
        SDL_CloseIO(io);
        return false;
    }

    SDL_Log("[AFS] %u entries, attributes %s", (unsigned int)afs.entry_count, has_attributes ? "found" : "not found");
#endif

    stream = io;
    return true;
}

bool AFS_Init(const char* file_path, size_t read_chunk_size) {
    SDL_assert(read_chunk_size > 0);
    _read_chunk_size = read_chunk_size;

    if (!init_afs(file_path)) {
        return false;
    }

    return true;
}

void AFS_Finish() {
#ifdef __vita__
    if (vita_fd >= 0) {
        sceIoClose(vita_fd);
        vita_fd = -1;
    }
#endif

    SDL_CloseIO(stream);
    stream = NULL;
    SDL_free((void*)afs.file_path);
    SDL_free(afs.entries);
    SDL_zero(afs);
    arrfree(requests);
}

size_t AFS_GetFileCount() {
    return afs.entry_count;
}

size_t AFS_GetSize(size_t file_num) {
    if (file_num >= afs.entry_count) {
        return 0;
    }

    return afs.entries[file_num].size;
}

// AFS reading

static int find_free_request_slot() {
    for (int i = 0; i < arrlen(requests); i++) {
        if (!requests[i].initialized) {
            return i;
        }
    }

    return arraddnindex(requests, 1);
}

static void read_into_request(ReadRequest* request, size_t max_read) {
    const Sint64 offset = afs.entries[request->file_num].offset + request->bytes_read;
    const size_t bytes_to_read = SDL_min(request->bytes_to_read, max_read);

#ifdef __vita__
    const double read_start = VitaPerf_Now();
    const size_t bytes_read = vita_read_at(offset, request->buf + request->bytes_read, bytes_to_read);
    const double read_time = VitaPerf_Now() - read_start;

    if (read_time > 2.0) {
        VitaPerf_Note("read file %d, %u bytes, %.1f ms", (int)request->file_num, (unsigned int)bytes_read, read_time);
    }
#else
    SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET);
    const size_t bytes_read = SDL_ReadIO(stream, request->buf + request->bytes_read, bytes_to_read);
#endif

    request->bytes_read += bytes_read;
    request->bytes_to_read -= bytes_read;

    if (request->bytes_to_read == 0) {
        request->state = AFS_READ_STATE_FINISHED;
    }
}

void AFS_RunServer() {
    int running_requests = 0;

    for (int i = 0; i < arrlen(requests); i++) {
        if (requests[i].state == AFS_READ_STATE_READING) {
            running_requests += 1;
        }
    }

    if (running_requests <= 0) {
        return;
    }

    const size_t max_read_per_request = _read_chunk_size / running_requests;

    for (int i = 0; i < arrlen(requests); i++) {
        ReadRequest* request = &requests[i];

        if (request->state != AFS_READ_STATE_READING) {
            continue;
        }

        read_into_request(request, max_read_per_request);
    }
}

AFSHandle AFS_Open(size_t file_num) {
    if (file_num >= afs.entry_count) {
        return AFS_NONE;
    }

    const int index = find_free_request_slot();
    ReadRequest* request = &requests[index];

    request->file_num = file_num;
    request->bytes_to_read = afs.entries[file_num].size;
    request->bytes_read = 0;
    request->buf = NULL;
    request->state = AFS_READ_STATE_IDLE;
    request->initialized = true;

    _log("Open %s (file_num = %d)", afs.entries[file_num].name, file_num);
    return index;
}

void AFS_Read(AFSHandle handle, void* buf) {
    ReadRequest* request = &requests[handle];
    _log("Read %s (bytes = 0x%X)", afs.entries[request->file_num].name, request->bytes_to_read);
    SDL_assert(request->buf == NULL);
    SDL_assert(request->state == AFS_READ_STATE_IDLE);
    request->buf = buf;
    request->state = AFS_READ_STATE_READING;
}

void AFS_ReadSync(AFSHandle handle, void* buf) {
    ReadRequest* request = &requests[handle];
    _log("Read (sync) %s", afs.entries[request->file_num].name);
    AFS_Read(handle, buf);
    read_into_request(request, request->bytes_to_read);
    SDL_assert(request->state == AFS_READ_STATE_FINISHED);
}

void AFS_Stop(AFSHandle handle) {
    ReadRequest* request = &requests[handle];
    _log("Stop %s", afs.entries[request->file_num].name);
    request->state = AFS_READ_STATE_IDLE;
}

void AFS_Close(AFSHandle handle) {
    ReadRequest* request = &requests[handle];
    _log("Close %s", afs.entries[request->file_num].name);
    SDL_zerop(request);
}

AFSReadState AFS_GetState(AFSHandle handle) {
    return requests[handle].state;
}
