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

#include <stdlib.h>
#include <stdio.h>

#include "platform.h"
#include "mytypes.h"
#include "utils.h"
#include "graphics.h"
#include "sound.h"
#include "events.h"


static uint32 lastTicks = 0x00ffffff;
static int paused   = 0;
static int maxSpeed = 0;
static int oneFrame = 0;

int evHotKeysEnabled = 0;

/*  Bounded run, for automated testing. 0 means unlimited, which is the default
 *  and the shipping behaviour: storyPlay() loops forever and only ever leaves
 *  through exit() on an input event. That is correct for a screensaver and
 *  impossible to assert on, because a test can only kill it and guess whether
 *  it was healthy. With `frames N` the engine shuts down cleanly through the
 *  normal path and returns 0, so a smoke test can check an exit code instead.
 */
uint32 evMaxFrames = 0;
static uint32 evFrameCount = 0;

/*  Max speed as a startup option, not just the <M> hotkey. The per-scene delay
 *  floor is 80 ms (12.5 FPS), so covering enough story iterations to exercise
 *  the island setup/teardown cycle takes minutes of wall clock at normal speed.
 *  The hotkey already proved the engine runs correctly unthrottled; this only
 *  makes that reachable without a keyboard.
 */
int evStartAtMaxSpeed = 0;

/*  Screensaver semantics: quit on mouse movement or a click, as Windows
 *  requires of a .scr running under /s. Off for ordinary runs.
 */
int evScreensaverMode = 0;

#define EV_MOUSE_DEADZONE 8

static int    evMouseSeen = 0;
static sint32 evMouseOriginX = 0;
static sint32 evMouseOriginY = 0;


static void eventsProcessEvents(void)
{
    PlatformEvent event;

    while (platformPollEvent(&event)) {

        switch(event.type) {

            case EVENT_KEY_DOWN:

                if (evHotKeysEnabled) {

                    switch (event.data.key.keycode) {

                        case KEY_SPACE:
                            paused = !paused;
                            break;

                        case KEY_M:
                            maxSpeed = !maxSpeed;
                            break;

                        case KEY_RETURN:
                            if (event.data.key.modifiers & KEYMOD_LALT) {
                                grToggleFullScreen();
                                oneFrame = 1;   // to redraw the window // TODO
                            }
                            else {
                                oneFrame = 1;
                            }
                            break;

                        case KEY_ESCAPE:
                            soundEnd();
                            graphicsEnd();
                            jcExit(255);
                            break;

                        default:
                            break;
                    }
                }
                else {
                    // Normal behaviour : no hot keys, the screen saver
                    // terminates if any key is pressed
                        soundEnd();
                        graphicsEnd();
                    jcExit(255);
                }
                break;

            /*  Mouse handling exists for one reason: a Windows screensaver must
             *  give way the moment the user touches the mouse. Outside
             *  screensaver mode these are ignored entirely, so windowed and
             *  `ttm`/`ads` runs behave exactly as before.
             */
            case EVENT_MOUSE_MOVE:
                if (evScreensaverMode) {
                    /*  A DEAD-ZONE, measured against the first position seen
                     *  rather than the last. The shell delivers a spurious
                     *  WM_MOUSEMOVE as the fullscreen window appears under the
                     *  pointer, and a tiny hardware jitter produces more, so
                     *  quitting on any movement closes the screensaver
                     *  immediately after it starts. Comparing against the first
                     *  sighting also means slow drift still accumulates past the
                     *  threshold rather than being reset each event.
                     */
                    if (!evMouseSeen) {
                        evMouseSeen = 1;
                        evMouseOriginX = event.data.mouse.x;
                        evMouseOriginY = event.data.mouse.y;
                    }
                    else {
                        sint32 dx = event.data.mouse.x - evMouseOriginX;
                        sint32 dy = event.data.mouse.y - evMouseOriginY;
                        if (dx < 0) dx = -dx;
                        if (dy < 0) dy = -dy;

                        if (dx > EV_MOUSE_DEADZONE || dy > EV_MOUSE_DEADZONE) {
                            soundEnd();
                            graphicsEnd();
                            jcExit(255);
                        }
                    }
                }
                break;

            case EVENT_MOUSE_BUTTON_DOWN:
                if (evScreensaverMode) {
                    soundEnd();
                    graphicsEnd();
                    jcExit(255);
                }
                break;

            /*  Something else took the foreground, so stop covering it. That is
             *  what DefScreenSaverProc does, and it was the one input rule this
             *  engine had no handler for at any layer.
             *
             *  Gated on evScreensaverMode like the mouse rules, which is what
             *  keeps a plain `jc_reborn.exe` usable as a background window: only
             *  /s sets that flag, so an ordinary run ignores focus entirely.
             *  No dead-zone equivalent is needed - losing activation is
             *  unambiguous in a way a two-pixel mouse twitch is not.
             */
            case EVENT_FOCUS_LOST:
                if (evScreensaverMode) {
                    soundEnd();
                    graphicsEnd();
                    jcExit(255);
                }
                break;

            case EVENT_WINDOW_REFRESH:
                grRefreshDisplay();
                break;

            case EVENT_QUIT:
                    soundEnd();
                    graphicsEnd();
                jcExit(255);
                break;

            default:
                break;
        }
    }
}


void eventsInit(void)
{
    lastTicks = platformGetTicks();
    maxSpeed  = evStartAtMaxSpeed;
    /*  Registered once per process. An embedding host may start the engine
     *  more than once in the same process (every screen saver activation), and
     *  atexit() has a small guaranteed registration limit. */
    {
        static int shutdownRegistered = 0;
        if (!shutdownRegistered) {
            atexit(platformShutdown);
            shutdownRegistered = 1;
        }
    }
}


void eventsWaitTick(uint16 delay)
{
    /*  WIDENED to 32 bits before the multiply. `delay *= 20` on the uint16
     *  parameter wrapped for any delay above 3276 ticks, so a script asking to
     *  wait 4000 ticks (80 s) slept 14.4 s instead. No shipped script asks for
     *  more than 660, so this has never fired, but the arithmetic was wrong and
     *  the clamp that hides it lives in a different file.
     */
    uint32 delayMs = (uint32)delay * 20u;

    oneFrame = 0;

    /*  Bounded run. Counted here because this is the one function every frame
     *  passes through, whichever mode is playing. Shutdown goes through the
     *  same soundEnd/graphicsEnd path as a user quit, so what the test exercises
     *  is the real teardown, not a shortcut around it. Exit code 0, because
     *  reaching the requested frame count is success; the input-driven quit
     *  paths keep their 255.
     */
    if (evMaxFrames) {
        /* Compare before incrementing: the maximum uint32 limit must stop
         * here instead of wrapping to zero. Ordinary runs stop on the same
         * call as the previous increment-then-greater-than comparison. */
        if (evFrameCount >= evMaxFrames) {
            /*  Say so on the way out. An exit code is enough for a native test,
             *  but in a browser there is no exit code to read: Emscripten is
             *  linked with -sNO_EXIT_RUNTIME, so exit() neither unloads the page
             *  nor invokes Module.onExit, and Module.calledRun only reports that
             *  STARTUP finished. Without this line there is no way to tell "still
             *  running" from "finished", which is exactly the distinction the
             *  bounded run exists to make.
             */
            printf("jc_reborn: stopping after %u frame(s) as requested\n",
                   (unsigned)evMaxFrames);
            fflush(stdout);
            soundEnd();
            graphicsEnd();
            jcExit(0);
        }
        ++evFrameCount;
    }

    /*  UNCONDITIONAL, once per frame, and it has to be outside the loop below.
     *
     *  That loop is the only place the engine ever gave control back to the
     *  host, and it is conditional: with maxSpeed set it never runs at all, and
     *  even at normal speed it is skipped entirely whenever the frame's own work
     *  already consumed delayMs. On the native backends that costs nothing,
     *  because the OS preempts. Under Emscripten there is no preemption and
     *  storyPlay never returns, so those two cases are a frozen browser tab.
     *
     *  A no-op everywhere except web. See platform.h.
     */
    platformFrameYield();

    eventsProcessEvents();

    while ((paused && !oneFrame)
            || (!maxSpeed && (platformGetTicks() - lastTicks < delayMs))) {
        platformDelay(5);
        eventsProcessEvents();
    }

    lastTicks = platformGetTicks();
}
