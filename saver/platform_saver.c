/*
 *  Johnny Reborn platform backend for a macOS screen saver bundle.
 *
 *  Differences from the upstream Cocoa backend (platform_macos.m):
 *
 *   - No window. legacyScreenSaver owns the windows; the engine renders into
 *     a software surface and every presented frame is copied to a front
 *     buffer that JohnnyCastawayView draws on the main thread.
 *   - No input. The screen saver framework ends the saver on input; the only
 *     event this backend ever reports is EVENT_QUIT, when the host asks.
 *   - No exit(). The engine runs on a worker thread of a host process; its
 *     exit paths come here through jcExit() -> platformEmbeddedExit(), which
 *     ends the thread instead of the process.
 *   - Audio is the same AudioQueue code as upstream.
 *
 *  Plain C on purpose: nothing here needs Objective-C.
 *
 *  This file is part of 'Johnny Castaway for macOS' (GPL-3.0-or-later).
 */

#ifdef PLATFORM_SAVER

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach_time.h>
#include <AudioToolbox/AudioToolbox.h>

#include "platform_soft.h"
#include "platform_saver.h"
#include "utils.h"

static const char* lastError = "";
void platformSoftSetError(const char* message) { lastError = message; }
const char* platformGetError(void) { return lastError; }

/* ── Front buffer, shared with the view ──────────────────────────────────── */

static pthread_mutex_t frameMutex = PTHREAD_MUTEX_INITIALIZER;
static uint8* frontPixels = NULL;
static int frontWidth = 0, frontHeight = 0, frontPitch = 0;
static volatile uint64_t frameSerial = 0;

/* ── Host control ────────────────────────────────────────────────────────── */

static volatile int quitRequested = 0;
static volatile int lastExitCode = 0;
static pthread_mutex_t fatalMutex = PTHREAD_MUTEX_INITIALIZER;
static char fatalText[1024];
static int haveFatal = 0;

void jcSaverRequestQuit(void) { quitRequested = 1; }
void jcSaverClearQuit(void) { quitRequested = 0; }
int  jcSaverQuitRequested(void) { return quitRequested; }

int jcSaverLockFrame(JCSaverFrame* frame) {
    pthread_mutex_lock(&frameMutex);
    if (!frontPixels) return 0;
    frame->pixels = frontPixels;
    frame->width = frontWidth;
    frame->height = frontHeight;
    frame->pitch = frontPitch;
    return 1;
}

void jcSaverUnlockFrame(void) { pthread_mutex_unlock(&frameMutex); }
uint64_t jcSaverFrameSerial(void) { return frameSerial; }

const char* jcSaverLastFatal(void) {
    const char* result;
    pthread_mutex_lock(&fatalMutex);
    result = haveFatal ? fatalText : NULL;
    pthread_mutex_unlock(&fatalMutex);
    return result;
}

void jcSaverClearFatal(void) {
    pthread_mutex_lock(&fatalMutex);
    haveFatal = 0;
    pthread_mutex_unlock(&fatalMutex);
}

int jcSaverLastExitCode(void) { return lastExitCode; }

void platformEmbeddedFatal(const char* message) {
    pthread_mutex_lock(&fatalMutex);
    snprintf(fatalText, sizeof(fatalText), "%s", message ? message : "unknown error");
    haveFatal = 1;
    pthread_mutex_unlock(&fatalMutex);
}

JCR_NORETURN void platformEmbeddedExit(int code) {
    lastExitCode = code;
    /* Never hold the frame lock here: the engine only takes it inside
     * platformUpdateWindow, which returns before any exit path runs. */
    pthread_exit(NULL);
}

/* ── Timing ──────────────────────────────────────────────────────────────── */

static mach_timebase_info_data_t timebaseInfo;
static uint64_t startTime;

int platformInit(void) {
    mach_timebase_info(&timebaseInfo);
    startTime = mach_absolute_time();
    return 0;
}

void platformShutdown(void) {}
void platformSetPreviewParent(void* parentWindowHandle) { UNUSED(parentWindowHandle); }

uint32 platformGetTicks(void) {
    uint64_t elapsed = mach_absolute_time() - startTime;
    return (uint32)((elapsed * timebaseInfo.numer) / (timebaseInfo.denom * 1000000ULL));
}

void platformDelay(uint32 ms) { usleep(ms * 1000); }
void platformFrameYield(void) {}

/* ── "Window": an offscreen surface plus the front-buffer copy ───────────── */

struct PlatformWindow {
    PlatformSurface* surface;
};

PlatformWindow* platformCreateWindow(const char* title, int width, int height, int fullscreen) {
    UNUSED(title); UNUSED(fullscreen);
    PlatformWindow* window = (PlatformWindow*)calloc(1, sizeof(PlatformWindow));
    if (!window) { lastError = "Out of memory allocating window"; return NULL; }
    window->surface = platformCreateSurface(width, height);
    if (!window->surface) { free(window); return NULL; }
    return window;
}

void platformDestroyWindow(PlatformWindow* window) {
    if (!window) return;
    /* The front buffer outlives the window on purpose: the view keeps showing
     * the last frame until the next engine run presents a new one. */
    platformFreeSurface(window->surface);
    free(window);
}

void platformShowCursor(int show) { UNUSED(show); }
void platformToggleFullscreen(PlatformWindow* window) { UNUSED(window); }

void platformUpdateWindow(PlatformWindow* window) {
    if (!window || !window->surface || !window->surface->pixels) return;
    PlatformSurface* s = window->surface;

    pthread_mutex_lock(&frameMutex);
    if (!frontPixels || frontWidth != s->width || frontHeight != s->height) {
        free(frontPixels);
        frontPitch = s->width * 4;
        frontPixels = (uint8*)malloc((size_t)frontPitch * (size_t)s->height);
        frontWidth = frontPixels ? s->width : 0;
        frontHeight = frontPixels ? s->height : 0;
    }
    if (frontPixels) {
        if (s->pitch == frontPitch) {
            memcpy(frontPixels, s->pixels, (size_t)frontPitch * (size_t)s->height);
        } else {
            for (int y = 0; y < s->height; y++)
                memcpy(frontPixels + (size_t)y * (size_t)frontPitch,
                       s->pixels + (size_t)y * (size_t)s->pitch,
                       (size_t)frontPitch);
        }
        frameSerial++;
    }
    pthread_mutex_unlock(&frameMutex);
}

PlatformSurface* platformGetWindowSurface(PlatformWindow* window) {
    return window ? window->surface : NULL;
}

/* ── Events: only the host's quit request ────────────────────────────────── */

int platformPollEvent(PlatformEvent* event) {
    if (quitRequested) {
        event->type = EVENT_QUIT;
        return 1;
    }
    event->type = EVENT_NONE;
    return 0;
}

/* ── Audio (AudioQueue, 8-bit unsigned PCM, as upstream) ─────────────────── */

static AudioQueueRef audioQueue = NULL;
static PlatformAudioCallback audioCallback = NULL;
static void* audioUserData = NULL;
static AudioQueueBufferRef audioBuffers[3];
static pthread_mutex_t audioMutex = PTHREAD_MUTEX_INITIALIZER;

static void audioOutputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    UNUSED(userData);
    if (audioCallback) {
        audioCallback(audioUserData, (uint8*)buffer->mAudioData, (int)buffer->mAudioDataBytesCapacity);
    } else {
        memset(buffer->mAudioData, 128, buffer->mAudioDataBytesCapacity);
    }
    buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

int platformInitAudio(void) { return 0; }

void platformCloseAudio(void) {
    if (audioQueue) {
        AudioQueueStop(audioQueue, true);
        AudioQueueDispose(audioQueue, true);
        audioQueue = NULL;
    }
    audioCallback = NULL;
    audioUserData = NULL;
}

int platformOpenAudio(PlatformAudioSpec* spec) {
    AudioStreamBasicDescription format;
    memset(&format, 0, sizeof(format));
    format.mSampleRate = spec->freq;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = 0;   /* 8-bit unsigned PCM, packed */
    format.mBitsPerChannel = 8;
    format.mChannelsPerFrame = spec->channels;
    format.mBytesPerFrame = spec->channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = spec->channels;

    audioCallback = spec->callback;
    audioUserData = spec->userdata;

    OSStatus status = AudioQueueNewOutput(&format, audioOutputCallback, NULL,
                                          NULL, NULL, 0, &audioQueue);
    if (status != noErr) {
        lastError = "Failed to create audio queue";
        audioQueue = NULL;
        return -1;
    }

    UInt32 bufferSize = (UInt32)spec->samples * spec->channels;
    for (int i = 0; i < 3; i++) {
        OSStatus bufStatus = AudioQueueAllocateBuffer(audioQueue, bufferSize, &audioBuffers[i]);
        if (bufStatus != noErr || audioBuffers[i] == NULL) {
            lastError = "Failed to allocate an audio queue buffer";
            AudioQueueDispose(audioQueue, true);
            audioQueue = NULL;
            return -1;
        }
        audioBuffers[i]->mAudioDataByteSize = bufferSize;
        audioOutputCallback(NULL, audioQueue, audioBuffers[i]);
    }
    return 0;
}

void platformPauseAudio(int pause) {
    if (!audioQueue) return;
    if (pause) AudioQueuePause(audioQueue);
    else AudioQueueStart(audioQueue, NULL);
}

void platformLockAudio(void) { pthread_mutex_lock(&audioMutex); }
void platformUnlockAudio(void) { pthread_mutex_unlock(&audioMutex); }

int platformLoadWAVFromMemory(const uint8* data, uint32 dataSize,
                              PlatformAudioSpec* spec,
                              uint8** audio_buf, uint32* audio_len) {
    if (!data || dataSize < 44) return -1;
    uint32 wavDataSize = (uint32)data[40] | ((uint32)data[41] << 8) |
                         ((uint32)data[42] << 16) | ((uint32)data[43] << 24);
    if (wavDataSize > dataSize - 44) wavDataSize = dataSize - 44;
    *audio_len = wavDataSize;
    *audio_buf = (uint8*)malloc(wavDataSize);
    if (!*audio_buf) return -1;
    memcpy(*audio_buf, data + 44, wavDataSize);
    spec->freq = (int)((uint32)data[24] | ((uint32)data[25] << 8) |
                        ((uint32)data[26] << 16) | ((uint32)data[27] << 24));
    spec->channels = (uint8)data[22];
    spec->format = (uint16)data[34] | ((uint16)data[35] << 8);
    return 0;
}

void platformFreeWAV(uint8* audio_buf) { free(audio_buf); }

#endif /* PLATFORM_SAVER */
