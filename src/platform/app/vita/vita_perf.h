#ifndef VITA_PERF_H
#define VITA_PERF_H

#include <stdbool.h>

/// Parts of a frame, in the order the main loop runs them
typedef enum VitaPerfSection {
    VITA_PERF_FILES, ///< Reading game data
    VITA_PERF_GAME,  ///< Game logic
    VITA_PERF_SOUND, ///< Music decoding
    VITA_PERF_DRAW,  ///< Sending the frame to the GPU
    VITA_PERF_WAIT,  ///< Waiting for the display
    VITA_PERF_FINISH,
    VITA_PERF_SECTION_COUNT,
} VitaPerfSection;

/// Start recording slow frames to `path`. Does nothing until this is called.
void VitaPerf_Init(const char* path);
void VitaPerf_Shutdown();

void VitaPerf_BeginFrame();
/// Mark the end of a section. Sections run in the order of `VitaPerfSection`
void VitaPerf_EndSection(VitaPerfSection section);
void VitaPerf_EndFrame();

/// Add a line about something that may have cost time in this frame, like a file read
void VitaPerf_Note(const char* fmt, ...);

/// Milliseconds since some fixed point, for timing a single call
double VitaPerf_Now();

#endif
