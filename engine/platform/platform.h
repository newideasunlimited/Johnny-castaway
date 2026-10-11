/*
 *  This file is part of 'Johnny Reborn'
 *
 *  An open-source engine for the classic
 *  'Johnny Castaway' screensaver by Sierra.
 *
 *  Copyright (C) 2019 Jeremie GUILLAUME
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef PLATFORM_H
#define PLATFORM_H

// Platform macros are defined by CMake build system:
// PLATFORM_WEB, PLATFORM_WINDOWS, PLATFORM_MACOS, PLATFORM_LINUX

#include <stddef.h>
#include "mytypes.h"

// Forward declarations
typedef struct PlatformSurface PlatformSurface;
typedef struct PlatformWindow PlatformWindow;
typedef struct PlatformRect PlatformRect;

// Platform rectangle
struct PlatformRect {
    int x;
    int y;
    int w;
    int h;
};

// Key codes (platform-independent)
typedef enum {
    KEY_UNKNOWN = 0,
    KEY_SPACE,
    KEY_RETURN,
    KEY_ESCAPE,
    KEY_M,
    KEY_LALT
} PlatformKeyCode;

// Event types
typedef enum {
    EVENT_NONE = 0,
    EVENT_QUIT,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_WINDOW_REFRESH,

    /*  Mouse input, added for screensaver behaviour. A Windows screensaver is
     *  required to exit on mouse movement and on any click, and the codebase had
     *  no mouse support at any layer: no WM_MOUSEMOVE handler, no mouse raw
     *  input, and no member here. Emitted by the Windows backend; the other
     *  three do not generate them, which is honest rather than a gap - only the
     *  Windows build is a .scr.
     */
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_BUTTON_DOWN,

    /*  Focus loss, for the same reason. The reference implementation Windows
     *  ships, DefScreenSaverProc, terminates on WM_ACTIVATE, WM_ACTIVATEAPP and
     *  WM_NCACTIVATE whenever the wParam says it is being deactivated - so if a
     *  UAC prompt, an alarm or an installer takes the foreground, the saver gets
     *  out of the way instead of sitting on top of it. This engine handled no
     *  focus message at all.
     *
     *  Windows-only, like the mouse events above: only the Windows build is a
     *  .scr, and the other three backends deliberately never emit it.
     */
    EVENT_FOCUS_LOST
} PlatformEventType;

// Key modifiers
typedef enum {
    KEYMOD_NONE = 0,
    KEYMOD_LALT = 1 << 0,
    KEYMOD_RALT = 1 << 1,
    KEYMOD_LSHIFT = 1 << 2,
    KEYMOD_RSHIFT = 1 << 3,
    KEYMOD_LCTRL = 1 << 4,
    KEYMOD_RCTRL = 1 << 5
} PlatformKeyMod;

// Platform event
typedef struct {
    PlatformEventType type;
    union {
        struct {
            PlatformKeyCode keycode;
            uint16 modifiers;
        } key;
        /*  Absolute position in client pixels. A screensaver needs the position
         *  rather than just "something moved", because the shell hands it a
         *  spurious WM_MOUSEMOVE as the window appears under the pointer: exiting
         *  on the first one would make the screensaver close the instant it
         *  started. The consumer applies a small dead-zone against the first
         *  position it sees.
         */
        struct {
            sint32 x;
            sint32 y;
        } mouse;
    } data;
} PlatformEvent;

// Platform initialization and shutdown
int platformInit(void);
void platformShutdown(void);

// Graphics - Window management
/*  Parent window for the next platformCreateWindow, or NULL for a normal
 *  top-level window. Set by the Windows screensaver preview switch (/p), where
 *  the shell supplies the HWND to draw into; a no-op on every other backend,
 *  since only the Windows build is a .scr.
 *
 *  A setter rather than a parameter: platformCreateWindow's signature is shared
 *  by all four backends, and a setter rather than an engine global because the
 *  platform layer must not reach into engine headers.
 */
void platformSetPreviewParent(void* parentWindowHandle);

PlatformWindow* platformCreateWindow(const char* title, int width, int height, int fullscreen);
void platformDestroyWindow(PlatformWindow* window);
void platformShowCursor(int show);
void platformToggleFullscreen(PlatformWindow* window);
void platformUpdateWindow(PlatformWindow* window);
PlatformSurface* platformGetWindowSurface(PlatformWindow* window);

// Graphics - Surface management
/* Constructors return NULL on failure and report platformGetError().
 * SurfaceFrom borrows pixels: the caller owns them on success and failure. */
PlatformSurface* platformCreateSurface(int width, int height);
PlatformSurface* platformCreateSurfaceFrom(void* pixels, int width, int height, int pitch);

void platformFreeSurface(PlatformSurface* surface);
void platformLockSurface(PlatformSurface* surface);
void platformUnlockSurface(PlatformSurface* surface);

// Graphics - Blitting and drawing
void platformBlitSurface(PlatformSurface* src, PlatformRect* srcRect,
                        PlatformSurface* dst, PlatformRect* dstRect);
void platformFillRect(PlatformSurface* surface, PlatformRect* rect,
                     uint8 r, uint8 g, uint8 b, uint8 a);
void platformSetClipRect(PlatformSurface* surface, PlatformRect* rect);
void platformGetClipRect(PlatformSurface* surface, PlatformRect* rect);
/*  platformMapRGB was declared here and implemented in all four backends, and
 *  called by absolutely nothing - verified across the whole tree. The Web one
 *  also returned RGB packing while every surface in that file is BGRA, so had it
 *  ever been wired up it would have been wrong on one platform out of four.
 *  Removed rather than left as an invitation. */

// Graphics - Surface access
uint8* platformGetSurfacePixels(PlatformSurface* surface);
int platformGetSurfacePitch(PlatformSurface* surface);
int platformGetSurfaceWidth(PlatformSurface* surface);
int platformGetSurfaceHeight(PlatformSurface* surface);
int platformGetSurfaceBytesPerPixel(PlatformSurface* surface);

// Events
int platformPollEvent(PlatformEvent* event);

// Timing
uint32 platformGetTicks(void);
void platformDelay(uint32 ms);

/*  Hand control back to the host once per frame.
 *
 *  A no-op on the native backends, which are preemptively scheduled and need
 *  nothing. It exists for Emscripten, where the engine's `while (1)` in
 *  storyPlay never returns and the ONLY yield in the entire program is the
 *  emscripten_sleep inside platformDelay - which sits inside a CONDITIONAL
 *  busy-wait in eventsWaitTick. Two consequences followed: with `maxspeed` the
 *  loop body never executes and the browser tab locks outright, and even at
 *  normal speed any frame whose own work already consumed its delay yields zero
 *  times. The latter is not hypothetical there, because the per-frame present
 *  copies ~1.2M pixels through a JavaScript loop.
 *
 *  Calling this once per frame makes the yield unconditional without
 *  restructuring the engine around emscripten_set_main_loop.
 */
void platformFrameYield(void);

// Audio
typedef void (*PlatformAudioCallback)(void* userdata, uint8* stream, int len);

typedef struct {
    int freq;
    uint16 format;
    uint8 channels;
    uint16 samples;
    PlatformAudioCallback callback;
    void* userdata;
} PlatformAudioSpec;

int platformInitAudio(void);
void platformCloseAudio(void);
int platformOpenAudio(PlatformAudioSpec* spec);
void platformPauseAudio(int pause);
void platformLockAudio(void);
void platformUnlockAudio(void);
int platformLoadWAVFromMemory(const uint8* data, uint32 dataSize,
                              PlatformAudioSpec* spec,
                              uint8** audio_buf, uint32* audio_len);
void platformFreeWAV(uint8* audio_buf);

// Optional helper: load a PNG from an in-memory buffer (32bpp BGRA surface).
// Returns NULL if not supported or if the data is invalid.
PlatformSurface* platformLoadPNGFromMemory(const uint8* data, size_t dataSize);

// Platform-specific error reporting
const char* platformGetError(void);

#endif // PLATFORM_H
