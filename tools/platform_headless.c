/*
 *  Headless backend for Johnny Reborn: no window, no audio device, no input.
 *
 *  Used to exercise the engine where there is no display (CI, a Linux dev box)
 *  and to render frames to disk. The engine's own options drive it:
 *
 *      jc_headless maxspeed frames 600 seed 7 capture last.ppm
 *
 *  Environment:
 *      JC_HEADLESS_DIR=<dir>   write every presented frame to <dir>/frame_NNNNN.ppm
 *      JC_HEADLESS_EVERY=<n>   ... but only every n-th frame (default 1)
 *
 *  This file is part of 'Johnny Reborn' (GPL-3.0-or-later).
 */

#ifdef PLATFORM_HEADLESS

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "platform_soft.h"

static const char* lastError = "";
void platformSoftSetError(const char* message) { lastError = message; }
const char* platformGetError(void) { return lastError; }

struct PlatformWindow {
    PlatformSurface* surface;
    unsigned long presented;
    const char* dumpDir;
    int dumpEvery;
};

static struct timespec startTime;

int platformInit(void) {
    clock_gettime(CLOCK_MONOTONIC, &startTime);
    return 0;
}

void platformShutdown(void) {}
void platformSetPreviewParent(void* parentWindowHandle) { UNUSED(parentWindowHandle); }

PlatformWindow* platformCreateWindow(const char* title, int width, int height, int fullscreen) {
    UNUSED(title); UNUSED(fullscreen);
    PlatformWindow* window = (PlatformWindow*)calloc(1, sizeof(PlatformWindow));
    if (!window) { lastError = "Out of memory allocating window"; return NULL; }
    window->surface = platformCreateSurface(width, height);
    if (!window->surface) { free(window); return NULL; }
    window->dumpDir = getenv("JC_HEADLESS_DIR");
    if (window->dumpDir && !*window->dumpDir) window->dumpDir = NULL;
    window->dumpEvery = 1;
    {
        const char* every = getenv("JC_HEADLESS_EVERY");
        if (every && atoi(every) > 0) window->dumpEvery = atoi(every);
    }
    return window;
}

void platformDestroyWindow(PlatformWindow* window) {
    if (!window) return;
    platformFreeSurface(window->surface);
    free(window);
}

void platformShowCursor(int show) { UNUSED(show); }
void platformToggleFullscreen(PlatformWindow* window) { UNUSED(window); }

static void writePPM(const PlatformSurface* s, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "headless: cannot write %s: %s\n", path, strerror(errno)); return; }
    fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);
    uint8* row = (uint8*)malloc((size_t)s->width * 3);
    for (int y = 0; y < s->height; y++) {
        const uint8* src = s->pixels + (size_t)y * (size_t)s->pitch;
        for (int x = 0; x < s->width; x++) {
            row[x * 3 + 0] = src[x * 4 + 2];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        fwrite(row, 3, (size_t)s->width, f);
    }
    free(row);
    fclose(f);
}

void platformUpdateWindow(PlatformWindow* window) {
    if (!window) return;
    window->presented++;
    if (window->dumpDir && (window->presented % (unsigned long)window->dumpEvery) == 0) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame_%05lu.ppm", window->dumpDir, window->presented);
        writePPM(window->surface, path);
    }
}

PlatformSurface* platformGetWindowSurface(PlatformWindow* window) {
    return window ? window->surface : NULL;
}

int platformPollEvent(PlatformEvent* event) {
    event->type = EVENT_NONE;
    return 0;
}

uint32 platformGetTicks(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32)((now.tv_sec - startTime.tv_sec) * 1000 +
                    (now.tv_nsec - startTime.tv_nsec) / 1000000);
}

void platformDelay(uint32 ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

void platformFrameYield(void) {}

/* Audio: accept the device, play nothing. The engine then runs its normal
 * sound logic (sample selection, timing) with no output. */
int platformInitAudio(void) { return 0; }
void platformCloseAudio(void) {}
int platformOpenAudio(PlatformAudioSpec* spec) { UNUSED(spec); return 0; }
void platformPauseAudio(int pause) { UNUSED(pause); }
void platformLockAudio(void) {}
void platformUnlockAudio(void) {}

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

#endif /* PLATFORM_HEADLESS */
