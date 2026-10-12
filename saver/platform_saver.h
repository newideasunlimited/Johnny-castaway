/*
 *  Host-side interface of the macOS screen saver backend (platform_saver.c).
 *
 *  The engine runs on a worker thread and draws into its own surface. Each
 *  time it presents a frame, the backend copies it into a front buffer that
 *  the ScreenSaverView reads from the main thread under a lock.
 *
 *  This file is part of 'Johnny Castaway for macOS' (GPL-3.0-or-later).
 */

#ifndef PLATFORM_SAVER_H
#define PLATFORM_SAVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *pixels;   /* premultiplied BGRA, top row first */
    int width;
    int height;
    int pitch;               /* bytes per row */
} JCSaverFrame;

/* The engine entry point (jc_reborn.c's main, renamed via -DJC_MAIN). */
int jcEngineMain(int argc, char **argv);

/* Ask the running engine to stop. It sees EVENT_QUIT on its next event poll,
 * tears down sound and graphics, and ends its thread. The request stays set
 * until jcSaverClearQuit() is called, so a late poll still sees it. */
void jcSaverRequestQuit(void);
void jcSaverClearQuit(void);
int  jcSaverQuitRequested(void);

/* Silence the audio output immediately, from any thread, without waiting for
 * the engine to notice a quit request. The engine's own sound logic keeps
 * running; the audio callback just emits silence while muted. Cleared by the
 * host before each engine start. */
void jcSaverSetMuted(int muted);

/* Front buffer access. jcSaverLockFrame() returns 1 and fills *frame when a
 * frame has been presented since the process started; the lock is held until
 * jcSaverUnlockFrame(), so copy quickly and unlock. */
int  jcSaverLockFrame(JCSaverFrame *frame);
void jcSaverUnlockFrame(void);

/* Increments on every presented frame; cheap to poll without the lock. */
uint64_t jcSaverFrameSerial(void);

/* Last fatalError() text from the engine, or NULL. Cleared on request. */
const char *jcSaverLastFatal(void);
void jcSaverClearFatal(void);

/* Exit code handed to jcExit() by the last engine run (255 = quit on input,
 * 0 = frame limit reached, 1 = fatal error). */
int jcSaverLastExitCode(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_SAVER_H */
