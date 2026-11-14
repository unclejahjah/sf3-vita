#include "port/sound/adx.h"

#if SOUND_ENABLED

#include "port/io/afs.h"
#include "port/io/resource.h"
#include "port/sound/adx_decoder.h"
#include "port/utils.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define SAMPLE_RATE 48000
#define N_CHANNELS 2
#define BYTES_PER_SAMPLE 2
#define MIN_QUEUED_DATA_MS 400
#define MIN_QUEUED_DATA (int)((float)SAMPLE_RATE * MIN_QUEUED_DATA_MS / 1000 * N_CHANNELS * BYTES_PER_SAMPLE)
#define TRACKS_MAX 10

/// When set, the main thread never waits for the memory card or for the sound device:
/// - music files are read by a loader thread, and a track starts to play once its file has arrived
/// - pausing is done here and the device keeps running. Asking SDL about the device, or pausing it,
///   waits for the sound thread, which can take a whole device period (21 ms on the Vita)
#if !defined(ADX_NONBLOCKING)
#if CRS_APP_DRIVER_VITA
#define ADX_NONBLOCKING 1
#else
#define ADX_NONBLOCKING 0
#endif
#endif

#if ADX_NONBLOCKING
typedef struct LoadJob {
    int file_id;
    Uint8* data;
    size_t size;
    bool done;
    bool succeeded;
    bool abandoned;
    struct LoadJob* next;
} LoadJob;
#endif

typedef struct ADXLoopInfo {
    Uint8* pcm;
    Uint32 pcm_size;
    Uint32 position;
} ADXLoopInfo;

typedef struct ADXTrack {
    size_t size;
    uint8_t* data;
    bool should_free_data_after_use;
    ADXDecoder decoder;
    SDL_IOStream* decoder_file;
    ADXLoopInfo loop_info;
#if ADX_NONBLOCKING
    LoadJob* load_job; // Not `NULL` while the file is still being read
    bool looping_allowed;
#endif
} ADXTrack;

static SDL_AudioStream* stream = NULL;
static ADXTrack tracks[TRACKS_MAX] = { 0 };
static int num_tracks = 0;
static int first_track_index = 0;
static bool has_tracks = false;

#if ADX_NONBLOCKING
/// The stream that plays on the device. `stream` is only a queue in front of it
static SDL_AudioStream* output = NULL;
static SDL_AtomicInt paused;

static SDL_Mutex* load_lock = NULL;
static SDL_Condition* load_wake = NULL;
static SDL_Thread* load_thread = NULL;
static LoadJob* load_first = NULL;
static LoadJob* load_last = NULL;
static bool load_quit = false;
#endif

static bool audio_available() {
    return stream != NULL;
}

static int stream_data_needed() {
    return MIN_QUEUED_DATA - SDL_GetAudioStreamQueued(stream);
}

static bool stream_is_empty() {
    return SDL_GetAudioStreamQueued(stream) <= 0;
}

static void* load_file(int file_id, size_t* size) {
    const size_t file_size = Resource_GetSize(file_id);
    *size = file_size;
    void* buff = SDL_malloc(file_size);

    ResourceHandle handle = Resource_Open(file_id);
    Resource_ReadSync(handle, buff);
    Resource_Close(handle);

    return buff;
}

#if ADX_NONBLOCKING
static void SDLCALL feed_output(void* userdata, SDL_AudioStream* target, int additional_amount, int total_amount) {
    Uint8 buffer[4096];

    while ((additional_amount > 0) && !SDL_GetAtomicInt(&paused)) {
        const int wanted = SDL_min(additional_amount, (int)sizeof(buffer));
        const int received = SDL_GetAudioStreamData(stream, buffer, wanted);

        if (received <= 0) {
            break;
        }

        SDL_PutAudioStreamData(target, buffer, received);
        additional_amount -= received;
    }
}

static void load_job_free(LoadJob* job) {
    SDL_free(job->data);
    SDL_free(job);
}

static int SDLCALL load_thread_main(void* userdata) {
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    SDL_LockMutex(load_lock);

    while (true) {
        while ((load_first == NULL) && !load_quit) {
            SDL_WaitCondition(load_wake, load_lock);
        }

        if (load_quit) {
            break;
        }

        LoadJob* job = load_first;
        load_first = job->next;

        if (load_first == NULL) {
            load_last = NULL;
        }

        if (job->abandoned) {
            load_job_free(job);
            continue;
        }

        SDL_UnlockMutex(load_lock);
        const bool succeeded = AFS_ReadFromWorker(job->file_id, job->data);
        SDL_LockMutex(load_lock);

        if (job->abandoned) {
            load_job_free(job);
        } else {
            job->succeeded = succeeded;
            job->done = true;
        }
    }

    SDL_UnlockMutex(load_lock);
    return 0;
}

static bool load_thread_start() {
    if (load_thread != NULL) {
        return true;
    }

    load_lock = SDL_CreateMutex();
    load_wake = SDL_CreateCondition();
    load_quit = false;
    load_thread = SDL_CreateThread(load_thread_main, "3sx music loader", NULL);
    return load_thread != NULL;
}

static void load_thread_stop() {
    if (load_thread == NULL) {
        return;
    }

    SDL_LockMutex(load_lock);
    load_quit = true;
    SDL_SignalCondition(load_wake);
    SDL_UnlockMutex(load_lock);
    SDL_WaitThread(load_thread, NULL);
    load_thread = NULL;

    // Tracks are destroyed before this, so whatever is left in the queue has been abandoned
    while (load_first != NULL) {
        LoadJob* job = load_first;
        load_first = job->next;
        load_job_free(job);
    }

    load_last = NULL;
    SDL_DestroyCondition(load_wake);
    SDL_DestroyMutex(load_lock);
    load_wake = NULL;
    load_lock = NULL;
}

/// Ask the loader thread for a file. Returns `NULL` if the file has to be read in place instead
static LoadJob* load_request(int file_id) {
    if ((file_id < 0) || ((size_t)file_id >= AFS_GetFileCount()) || !load_thread_start()) {
        return NULL;
    }

    LoadJob* job = SDL_calloc(1, sizeof(LoadJob));
    job->file_id = file_id;
    job->size = AFS_GetSize(file_id);
    job->data = SDL_malloc(job->size);

    SDL_LockMutex(load_lock);

    if (load_last != NULL) {
        load_last->next = job;
    } else {
        load_first = job;
    }

    load_last = job;
    SDL_SignalCondition(load_wake);
    SDL_UnlockMutex(load_lock);
    return job;
}

/// Give up on a file. Whoever touches the job last frees it
static void load_abandon(LoadJob* job) {
    SDL_LockMutex(load_lock);

    if (job->done) {
        load_job_free(job);
    } else {
        job->abandoned = true;
    }

    SDL_UnlockMutex(load_lock);
}

static bool load_is_done(LoadJob* job) {
    SDL_LockMutex(load_lock);
    const bool done = job->done;
    SDL_UnlockMutex(load_lock);
    return done;
}
#endif

static bool track_reached_eof(ADXTrack* track) {
    return track->decoder.sample_index >= track->decoder.header.total_samples;
}

static bool track_loop_filled(ADXTrack* track) {
    if (track->loop_info.pcm != NULL) {
        return track->decoder.sample_index >= track->decoder.header.loop_end_sample;
    } else {
        return false;
    }
}

static bool track_needs_decoding(ADXTrack* track) {
    if (track->loop_info.pcm != NULL) {
        return !track_loop_filled(track);
    } else {
        return !track_reached_eof(track);
    }
}

static bool track_exhausted(ADXTrack* track) {
#if ADX_NONBLOCKING
    if (track->load_job != NULL) {
        return false; // Nothing has been played yet
    }
#endif

    if (track->loop_info.pcm != NULL) {
        return false; // Track is never exhausted, because it can be looped infinitely
    } else {
        return track_reached_eof(track);
    }
}

static Uint32 track_add_samples_to_loop(
    ADXTrack* track, const Sint16* buffer, Uint32 sample_count, Uint32 buffer_start_sample
) {
    ADXLoopInfo* loop_info = &track->loop_info;
    const ADXHeader* header = &track->decoder.header;

    if (loop_info->pcm == NULL) {
        return 0; // No need to add samples if looping is not enabled
    }

    const Uint32 buffer_end_sample = buffer_start_sample + sample_count;
    const Uint32 copy_start = SDL_max(buffer_start_sample, header->loop_begin_sample);
    const Uint32 copy_end = SDL_min(buffer_end_sample, header->loop_end_sample);

    if (copy_end > copy_start) {
        const Uint32 copy_samples = copy_end - copy_start;
        const Uint32 source_offset = (copy_start - buffer_start_sample) * N_CHANNELS;
        const Uint32 copy_size = copy_samples * N_CHANNELS * BYTES_PER_SAMPLE;
        SDL_memcpy(loop_info->pcm + loop_info->position, buffer + source_offset, copy_size);
        loop_info->position += copy_size;

        if (loop_info->position == loop_info->pcm_size) {
            loop_info->position = 0;
        }
    }

    return buffer_end_sample > header->loop_end_sample ? buffer_end_sample - header->loop_end_sample : 0;
}

static void loop_info_init(ADXLoopInfo* info, const ADXHeader* header) {
    if (!header->loop_enabled) {
        return;
    }

    info->pcm_size = (header->loop_end_sample - header->loop_begin_sample) * BYTES_PER_SAMPLE * N_CHANNELS;
    info->pcm = SDL_malloc(info->pcm_size);
    info->position = 0;
}

static void loop_info_destroy(ADXLoopInfo* info) {
    if (info->pcm) {
        SDL_free(info->pcm);
    }

    SDL_zerop(info);
}

static void track_open(ADXTrack* track, bool looping_allowed) {
    track->decoder_file = SDL_IOFromConstMem(track->data, track->size);

    if (track->decoder_file == NULL || !ADXDecoder_Init(&track->decoder, track->decoder_file)) {
        fatal_error("Failed to initialize ADX decoder: %s", SDL_GetError());
    }

    SDL_zerop(&track->loop_info);

    if (looping_allowed) {
        loop_info_init(&track->loop_info, &track->decoder.header);
    }
}

/// @return `true` if the track has its file and can be decoded
static bool track_finish_loading(ADXTrack* track) {
#if ADX_NONBLOCKING
    LoadJob* job = track->load_job;

    if (job == NULL) {
        return true;
    }

    if (!load_is_done(job)) {
        return false;
    }

    if (!job->succeeded) {
        fatal_error("Failed to read ADX file %d.", job->file_id);
    }

    track->data = job->data;
    track->size = job->size;
    track->load_job = NULL;
    SDL_free(job);
    track_open(track, track->looping_allowed);
#endif

    return true;
}

static void process_track(ADXTrack* track) {
    if (!track_finish_loading(track)) {
        return;
    }

    if ((stream_data_needed() > 0) && track_needs_decoding(track)) {
        const Uint32 samples_needed = stream_data_needed() / (BYTES_PER_SAMPLE * N_CHANNELS);

        if (samples_needed == 0) {
            return;
        }

        const Uint32 sample_size = samples_needed * N_CHANNELS * BYTES_PER_SAMPLE;
        const Uint32 buffer_start_sample = track->decoder.sample_index;
        Sint16* buffer = SDL_malloc(sample_size);
        const Uint32 decoded_samples = ADXDecoder_Decode(&track->decoder, buffer, samples_needed);

        if (track->decoder.header.channel_count == 1) {
            for (Uint32 i = decoded_samples; i > 0; i--) {
                const Sint16 sample = buffer[i - 1];
                buffer[(i - 1) * N_CHANNELS] = sample;
                buffer[(i - 1) * N_CHANNELS + 1] = sample;
            }
        }

        const Uint32 overflow = track_add_samples_to_loop(track, buffer, decoded_samples, buffer_start_sample);
        SDL_PutAudioStreamData(stream, buffer, (decoded_samples - overflow) * BYTES_PER_SAMPLE * N_CHANNELS);
        SDL_free(buffer);
    }

    // Queue looped samples (if needed)
    while (track_loop_filled(track) && (stream_data_needed() > 0)) {
        const int available_data = track->loop_info.pcm_size - track->loop_info.position;
        const int data_to_queue = SDL_min(stream_data_needed(), available_data);
        SDL_PutAudioStreamData(stream, track->loop_info.pcm + track->loop_info.position, data_to_queue);
        track->loop_info.position += data_to_queue;

        if (track->loop_info.position == track->loop_info.pcm_size) {
            track->loop_info.position = 0;
        }
    }
}

static void track_init(ADXTrack* track, int file_id, void* buf, size_t buf_size, bool looping_allowed) {
    if (file_id == -1 && buf == NULL) {
        fatal_error("One of file_id or buf must be valid.");
    }

    if (file_id != -1) {
#if ADX_NONBLOCKING
        track->load_job = load_request(file_id);

        if (track->load_job != NULL) {
            track->should_free_data_after_use = true;
            track->looping_allowed = looping_allowed;
            return;
        }
#endif

        track->data = load_file(file_id, &track->size);
        track->should_free_data_after_use = true;
    } else {
        track->data = buf;
        track->size = buf_size;
        track->should_free_data_after_use = false;
    }

    track_open(track, looping_allowed);
    process_track(track); // Feed first batch of data to the stream
}

static void track_destroy(ADXTrack* track) {
#if ADX_NONBLOCKING
    if (track->load_job != NULL) {
        load_abandon(track->load_job);
        SDL_zerop(track);
        return;
    }
#endif

    loop_info_destroy(&track->loop_info);
    SDL_CloseIO(track->decoder_file);

    if (track->should_free_data_after_use) {
        SDL_free(track->data);
    }

    SDL_zerop(track);
}

static ADXTrack* alloc_track() {
    if (num_tracks >= TRACKS_MAX) {
        fatal_error("Too many queued ADX tracks.");
    }

    const int index = (first_track_index + num_tracks) % TRACKS_MAX;
    num_tracks += 1;
    has_tracks = true;
    return &tracks[index];
}

void ADX_ProcessTracks() {
    if (!audio_available()) {
        return;
    }

    const int first_track_index_old = first_track_index;
    const int num_tracks_old = num_tracks;

    for (int i = 0; i < num_tracks_old; i++) {
        const int j = (first_track_index_old + i) % TRACKS_MAX;
        ADXTrack* track = &tracks[j];
        process_track(track);

        if (!track_exhausted(track)) {
            // No need to continue if the current track is not exhausted yet
            break;
        }

        track_destroy(track);
        num_tracks -= 1;

        if (num_tracks > 0) {
            first_track_index = (first_track_index + 1) % TRACKS_MAX;
        } else {
            first_track_index = 0;
        }
    }
}

void ADX_Init() {
    const SDL_AudioSpec spec = { .format = SDL_AUDIO_S16, .channels = N_CHANNELS, .freq = SAMPLE_RATE };

#if ADX_NONBLOCKING
    SDL_SetAtomicInt(&paused, 1);
    output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_output, NULL);

    if (output != NULL) {
        stream = SDL_CreateAudioStream(&spec, &spec);

        if (stream != NULL) {
            SDL_ResumeAudioStreamDevice(output);
        } else {
            SDL_DestroyAudioStream(output);
            output = NULL;
        }
    }
#else
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
#endif

    if (stream == NULL) {
        SDL_Log("Failed to create audio stream: %s; continuing without sound", SDL_GetError());
    }
}

void ADX_Exit() {
    if (!audio_available()) {
        return;
    }

    ADX_Stop();

#if ADX_NONBLOCKING
    load_thread_stop();
    SDL_DestroyAudioStream(output); // First, because its callback reads from `stream`
    output = NULL;
#endif

    SDL_DestroyAudioStream(stream);
    stream = NULL;
}

void ADX_Stop() {
    if (!audio_available()) {
        return;
    }

    ADX_Pause(true);
    SDL_ClearAudioStream(stream);

    for (int i = 0; i < num_tracks; i++) {
        const int j = (first_track_index + i) % TRACKS_MAX;
        track_destroy(&tracks[j]);
    }

    num_tracks = 0;
    first_track_index = 0;
    has_tracks = false;
}

int ADX_IsPaused() {
    if (!audio_available()) {
        return 1;
    }

#if ADX_NONBLOCKING
    return SDL_GetAtomicInt(&paused);
#else
    return SDL_AudioStreamDevicePaused(stream);
#endif
}

void ADX_Pause(int pause) {
    if (!audio_available()) {
        return;
    }

#if ADX_NONBLOCKING
    SDL_SetAtomicInt(&paused, pause ? 1 : 0);
#else
    if (pause) {
        SDL_PauseAudioStreamDevice(stream);
    } else {
        SDL_ResumeAudioStreamDevice(stream);
    }
#endif
}

void ADX_StartMem(void* buf, size_t size) {
    if (!audio_available()) {
        return;
    }

    ADX_Stop();

    ADXTrack* track = alloc_track();
    track_init(track, -1, buf, size, true);
}

int ADX_GetNumFiles() {
    if (!audio_available()) {
        return 0;
    }

    return num_tracks;
}

void ADX_EntryAfs(int file_id) {
    if (!audio_available()) {
        return;
    }

    ADXTrack* track = alloc_track();
    track_init(track, file_id, NULL, 0, false);
}

void ADX_StartSeamless() {
    if (!audio_available()) {
        return;
    }

    ADX_Pause(false);
}

void ADX_ResetEntry() {
    // ResetEntry is always called after Stop, so we don't need to do anything here
}

void ADX_StartAfs(int file_id) {
    if (!audio_available()) {
        return;
    }

    ADX_Stop();

    ADXTrack* track = alloc_track();
    track_init(track, file_id, NULL, 0, true);
}

void ADX_SetOutVol(int volume) {
    if (!audio_available()) {
        return;
    }

    // Convert volume (dB * 10) to linear gain
    const float gain = powf(10.0f, volume / 200.0f);
#if ADX_NONBLOCKING
    SDL_SetAudioStreamGain(output, gain);
#else
    SDL_SetAudioStreamGain(stream, gain);
#endif
}

ADXState ADX_GetState() {
    if (!audio_available()) {
        return ADX_STATE_STOP;
    }

    if (!has_tracks) {
        return ADX_STATE_STOP;
    }

#if ADX_NONBLOCKING
    const bool loading = (num_tracks > 0) && (tracks[first_track_index].load_job != NULL);
#else
    const bool loading = false;
#endif

    if (stream_is_empty() && !loading) {
        return ADX_STATE_PLAYEND;
    } else {
        if (ADX_IsPaused()) {
            return ADX_STATE_STOP;
        } else {
            return ADX_STATE_PLAYING;
        }
    }
}

#else

void ADX_ProcessTracks() {
    // Do nothing
}

void ADX_Init() {
    // Do nothing
}

void ADX_Exit() {
    // Do nothing
}

void ADX_Stop() {
    // Do nothing
}

int ADX_IsPaused() {
    return 1;
}

void ADX_Pause(int pause) {
    // Do nothing
}

void ADX_StartSeamless() {
    // Do nothing
}

void ADX_StartMem(void* buf, size_t size) {
    // Do nothing
}

int ADX_GetNumFiles() {
    return 0;
}

void ADX_EntryAfs(int file_id) {
    // Do nothing
}

void ADX_StartAfs(int file_id) {
    // Do nothing
}

void ADX_ResetEntry() {
    // Do nothing
}

void ADX_SetOutVol(int volume) {
    // Do nothing
}

ADXState ADX_GetState() {
    return ADX_STATE_STOP;
}

#endif
