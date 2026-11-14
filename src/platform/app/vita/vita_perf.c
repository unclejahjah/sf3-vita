/**
 * @brief Slow-frame recorder for the PS Vita.
 *
 * Times the parts of every frame and writes the frames that missed the display's 16.7 ms to a text file,
 * with a summary every ten seconds. The file is written by a second thread, so recording does not
 * itself cost a frame.
 */

#if CRS_APP_DRIVER_VITA

#include "platform/app/vita/vita_perf.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>

#define BUFFER_SIZE (32 * 1024)
#define SUMMARY_INTERVAL 600
/// A frame that takes longer than this between two display waits has missed a refresh
#define SLOW_FRAME_MS 18.0
/// Work that takes longer than this is close to missing one
#define SLOW_WORK_MS 13.0
/// Slow frames written in full per summary interval. The rest are only counted
#define SLOW_FRAMES_LOGGED_MAX 60

static const char* section_names[VITA_PERF_SECTION_COUNT] = { "files", "game", "sound", "draw", "wait", "finish" };

static bool enabled = false;
static char* file_path = NULL;

// Text waiting to be handed to the writer thread. Only the main thread touches it
static char buffer[BUFFER_SIZE];
static size_t buffer_length = 0;

// Text owned by the writer thread while `writing` is set
static char write_buffer[BUFFER_SIZE];
static size_t write_length = 0;
static SDL_AtomicInt writing;
static SDL_AtomicInt quitting;
static SDL_Semaphore* write_requested = NULL;
static SDL_Thread* writer = NULL;

static Uint64 frame_number = 0;
static double frame_start = 0;
static double previous_frame_start = 0;
static double section_start = 0;
static double sections[VITA_PERF_SECTION_COUNT];

static double section_sum[VITA_PERF_SECTION_COUNT];
static double section_max[VITA_PERF_SECTION_COUNT];
static double interval_max = 0;
static int slow_frames = 0;
static int frames_in_summary = 0;

double VitaPerf_Now() {
    return (double)SDL_GetTicksNS() / 1000000.0;
}

static void append(const char* fmt, ...) {
    if (buffer_length >= BUFFER_SIZE - 1) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    const int written = SDL_vsnprintf(buffer + buffer_length, BUFFER_SIZE - buffer_length, fmt, args);
    va_end(args);

    if (written > 0) {
        buffer_length = SDL_min(buffer_length + (size_t)written, (size_t)BUFFER_SIZE - 1);
    }
}

static int SDLCALL writer_main(void* userdata) {
    while (true) {
        SDL_WaitSemaphore(write_requested);

        if (SDL_GetAtomicInt(&writing)) {
            FILE* file = fopen(file_path, "a");

            if (file != NULL) {
                fwrite(write_buffer, 1, write_length, file);
                fclose(file);
            }

            SDL_SetAtomicInt(&writing, 0);
        }

        if (SDL_GetAtomicInt(&quitting)) {
            break;
        }
    }

    return 0;
}

/// Hand the waiting text to the writer thread, unless it is still busy with the last batch
static void flush() {
    if ((buffer_length == 0) || SDL_GetAtomicInt(&writing)) {
        return;
    }

    SDL_memcpy(write_buffer, buffer, buffer_length);
    write_length = buffer_length;
    buffer_length = 0;
    SDL_SetAtomicInt(&writing, 1);
    SDL_SignalSemaphore(write_requested);
}

void VitaPerf_Init(const char* path) {
    file_path = SDL_strdup(path);

    // Start a new file on every run
    FILE* file = fopen(file_path, "w");

    if (file == NULL) {
        return;
    }

    fprintf(file, "Times are in milliseconds. A frame has 16.7 ms.\n");
    fclose(file);

    write_requested = SDL_CreateSemaphore(0);
    writer = SDL_CreateThread(writer_main, "3sx perf log", NULL);

    if ((write_requested == NULL) || (writer == NULL)) {
        return;
    }

    enabled = true;
}

void VitaPerf_Shutdown() {
    if (!enabled) {
        return;
    }

    enabled = false;

    // Let a batch that is being written finish, then write what is left
    while (SDL_GetAtomicInt(&writing)) {
        SDL_Delay(1);
    }

    flush();
    SDL_SetAtomicInt(&quitting, 1);
    SDL_SignalSemaphore(write_requested);
    SDL_WaitThread(writer, NULL);
    writer = NULL;
}

void VitaPerf_BeginFrame() {
    if (!enabled) {
        return;
    }

    previous_frame_start = frame_start;
    frame_start = VitaPerf_Now();
    section_start = frame_start;
    SDL_zeroa(sections);
}

void VitaPerf_EndSection(VitaPerfSection section) {
    if (!enabled) {
        return;
    }

    const double now = VitaPerf_Now();
    sections[section] = now - section_start;
    section_start = now;
}

static void write_summary() {
    append("summary frames %llu-%llu:", (unsigned long long)(frame_number - frames_in_summary + 1),
           (unsigned long long)frame_number);
    append(" slow=%d longest=%.1f |", slow_frames, interval_max);

    for (int i = 0; i < VITA_PERF_SECTION_COUNT; i++) {
        append(" %s avg=%.2f max=%.1f", section_names[i], section_sum[i] / frames_in_summary, section_max[i]);
    }

    append("\n");

    SDL_zeroa(section_sum);
    SDL_zeroa(section_max);
    interval_max = 0;
    slow_frames = 0;
    frames_in_summary = 0;
}

void VitaPerf_EndFrame() {
    if (!enabled) {
        return;
    }

    frame_number += 1;
    frames_in_summary += 1;

    // Time since the previous frame started, which is what the player sees
    const double interval = (previous_frame_start > 0) ? (frame_start - previous_frame_start) : 0;
    double work = 0;

    for (int i = 0; i < VITA_PERF_SECTION_COUNT; i++) {
        section_sum[i] += sections[i];
        section_max[i] = SDL_max(section_max[i], sections[i]);

        if (i != VITA_PERF_WAIT) {
            work += sections[i];
        }
    }

    interval_max = SDL_max(interval_max, interval);

    if ((interval > SLOW_FRAME_MS) || (work > SLOW_WORK_MS)) {
        slow_frames += 1;

        if (slow_frames <= SLOW_FRAMES_LOGGED_MAX) {
            append("frame %llu: since_last=%.1f work=%.1f |", (unsigned long long)frame_number, interval, work);

            for (int i = 0; i < VITA_PERF_SECTION_COUNT; i++) {
                append(" %s=%.1f", section_names[i], sections[i]);
            }

            append("\n");
        }
    }

    if (frames_in_summary >= SUMMARY_INTERVAL) {
        write_summary();
        flush();
    }
}

void VitaPerf_Note(const char* fmt, ...) {
    if (!enabled) {
        return;
    }

    char line[256];
    va_list args;
    va_start(args, fmt);
    SDL_vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    append("  note (frame %llu): %s\n", (unsigned long long)(frame_number + 1), line);
}

#endif // CRS_APP_DRIVER_VITA
