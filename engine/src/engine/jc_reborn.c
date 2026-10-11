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
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "mytypes.h"
#include "utils.h"
#include "resource.h"
#include "dump.h"
#include "graphics.h"
#include "events.h"
#include "sound.h"
#include "ttm.h"
#include "ads.h"
#include "story.h"
#include "zipvfs.h"
#include "config.h"
#include "art_style.h"
#include "jc_resources.h"

#define MAX_ARGS 3

static int  argDump     = 0;
static int  argBench    = 0;
static int  argTtm      = 0;
static int  argAds      = 0;
static int  argPlayAll  = 0;
static int  argIsland   = 0;
static int  argMinimize = 0;
static const char *argStyle = NULL;
static const char *argSetStyle = NULL;

/*  Windows screensaver mode, selected by the shell's /s, /c and /p switches.
 *  SCR_MODE_NONE is every other invocation, including every non-Windows one.
 */
#define SCR_MODE_NONE     0
#define SCR_MODE_RUN      1   /* /s  - run fullscreen as the screensaver      */
#define SCR_MODE_CONFIG   2   /* /c  - show the settings dialog               */
#define SCR_MODE_PREVIEW  3   /* /p  - draw into the shell's preview window   */

/*  Stays outside the guard: the EXPECT_PARENT_HWND case in parseArgs is part of
 *  the shared token state machine and assigns it on every platform. Only the
 *  READ of it is Windows-only. */
static unsigned long long argParentHwnd = 0;

/*  Inside the guard, because every read and every write of it is too. Outside,
 *  GCC 13 reports
 *      warning: 'argScreensaver' defined but not used
 *  on every Linux build - which is correct, and is the sort of warning that
 *  trains people to ignore warnings. */
#if defined(_WIN32)

static int argScreensaver = SCR_MODE_NONE;

/*  Match a screensaver switch. Windows is inconsistent about these across
 *  versions and shells: the letter may be upper or lower case, the prefix may be
 *  '/' or '-', and /c in particular arrives both bare and as "/c:12345". Accept
 *  every documented spelling rather than guessing one.
 */
static int isScreensaverSwitch(const char *arg, char letter)
{
    if (!arg || (arg[0] != '/' && arg[0] != '-'))
        return 0;
    if (tolower((unsigned char)arg[1]) != letter)
        return 0;
    /* Nothing after the letter, or a colon introducing a handle. */
    return arg[2] == '\0' || arg[2] == ':';
}
#endif

static char *args[MAX_ARGS];
static int  numArgs  = 0;


static int isMinimizeArg(const char *arg)
{
    if (!arg) return 0;
    return (!strcmp(arg, "minimize") ||
            !strcmp(arg, "-minimize") ||
            !strcmp(arg, "--minimize") ||
            !strcmp(arg, "/minimize"));
}

static void normalizeToken(const char *in, char *out, size_t outSize)
{
    size_t j = 0;
    if (!out || outSize == 0)
        return;

    if (!in) {
        out[0] = '\0';
        return;
    }

    for (size_t i = 0; in[i] && (j + 1) < outSize; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c)) {
            out[j++] = (char)tolower(c);
        }
    }

    out[j] = '\0';
}

static int pickRandomHoliday(void)
{
    // We pick a single random holiday at startup.
    // Keep it independent from the main rand() stream, which is seeded later
    // (graphicsInit()), so this doesn't perturb story/scene randomness.
    unsigned long long t = (unsigned long long)time(NULL);
    unsigned long long c = (unsigned long long)clock();
    unsigned long long mix = t ^ (t >> 32) ^ c ^ (c << 16);
    unsigned int x = (unsigned int)(mix ^ (mix >> 32));

    // 1..4 map to: Halloween, St Patrick, Christmas, New Year
    return 1 + (int)(x % 4);
}

static int parseHolidayToken(const char *arg, int *outHoliday)
{
    if (!arg || !outHoliday)
        return 0;

    char tok[64];
    normalizeToken(arg, tok, sizeof(tok));
    if (!tok[0])
        return 0;

    // Accept numeric 0..4
    if (tok[0] >= '0' && tok[0] <= '4' && tok[1] == '\0') {
        *outHoliday = tok[0] - '0';
        return 1;
    }

    // Automatic (calendar-based)
    if (!strcmp(tok, "auto") || !strcmp(tok, "calendar") ||
        !strcmp(tok, "date") || !strcmp(tok, "system")) {
        *outHoliday = -1;
        return 1;
    }

    // Choose a random holiday at startup
    if (!strcmp(tok, "random") || !strcmp(tok, "rand") || !strcmp(tok, "any")) {
        *outHoliday = -2;
        return 1;
    }

    // Explicitly disable
    if (!strcmp(tok, "none") || !strcmp(tok, "no") ||
        !strcmp(tok, "off") || !strcmp(tok, "noholiday")) {
        *outHoliday = 0;
        return 1;
    }

    // Supported holidays (see story.c / island.c)
    if (!strcmp(tok, "halloween")) {
        *outHoliday = 1;
        return 1;
    }

    if (!strcmp(tok, "stpatricks") || !strcmp(tok, "stpatrick") ||
        !strcmp(tok, "stpatricksday") || !strcmp(tok, "stpatrickday")) {
        *outHoliday = 2;
        return 1;
    }

    if (!strcmp(tok, "christmas") || !strcmp(tok, "xmas")) {
        *outHoliday = 3;
        return 1;
    }

    if (!strcmp(tok, "newyear") || !strcmp(tok, "newyears") ||
        !strcmp(tok, "newyearseve")) {
        *outHoliday = 4;
        return 1;
    }

    return 0;
}

static int isHolidayArg(const char *arg)
{
    if (!arg)
        return 0;

    return (!strcmp(arg, "holiday") ||
            !strcmp(arg, "-holiday") ||
            !strcmp(arg, "--holiday") ||
            !strcmp(arg, "/holiday"));
}


static void minimizeConsoleWindow(void)
{
#if defined(_WIN32)
    HWND console = GetConsoleWindow();
    if (!console) {
        return;
    }

    /*
     * If the user explicitly asks for "minimize", do it unconditionally.
     * (Heuristics based on console ownership are surprising for explicit
     * opt-in use cases like shortcuts, startup scripts, etc.)
     */
    ShowWindow(console, SW_MINIMIZE);
#endif
}


static void usage(void)
{
    printf("\n");
    printf(" Usage :\n");
    printf("         jc_reborn\n");
    printf("         jc_reborn help\n");
    printf("         jc_reborn version\n");
    printf("         jc_reborn dump\n");
    printf("         jc_reborn [<options>] bench\n");
    printf("         jc_reborn [<options>] ttm <TTM name>\n");
    printf("         jc_reborn [<options>] ads <ADS name> <ADS tag no>\n");
    printf("\n");
    printf(" Available options are:\n");
    printf("         window     - play in windowed mode\n");
    printf("         minimize   - start with the console window minimized (Windows)\n");
    printf("         nosound    - quiet mode\n");
    printf("         island     - display the island as background for ADS play\n");
    printf("         debug      - print some debug info on stdout\n");
    printf("         hotkeys    - enable hot keys\n");
    printf("         seed <n>   - fix the random seed, for reproducible runs\n");
    printf("         frames <n> - stop cleanly after n frames (exit code 0)\n");
    printf("         style <id> - use hd or cartoon for this run\n");
    printf("         setstyle <id> - save the style and exit without advancing the story\n");
    printf("         capture <file.ppm> - save the final rendered frame on clean shutdown\n");
    printf("         maxspeed   - run unthrottled from the start (as <M> does)\n");
    printf("         night      - force the night backdrop (default: 21:00-05:59)\n");
    printf("         day        - force daytime, ignoring the clock\n");
    printf("         holiday <name> - force holiday decorations (halloween|stpatricks|christmas|newyear|random|none|auto)\n");
    printf("         (shorthand) halloween|stpatricks|christmas|newyear|random\n");
    printf("\n");
    printf(" While-playing hot-keys (if enabled):\n");
    printf("         Esc        - Terminate immediately\n");
    printf("         Alt+Return - Toggle full screen / windowed mode\n");
    printf("         Space      - Toggle pause / unpause\n");
    printf("         Return     - When paused, advance one frame\n");
    printf("         <M>        - toggle max / normal speed\n");
    printf("\n");
    jcExit(1);
}


/*  JC_VERSION comes from CMakeLists.txt's project(... VERSION ...) as a quoted
 *  compile definition. The fallback exists so a hand-rolled build (the vs/
 *  projects, or a bare cc invocation) still compiles and is honest about not
 *  knowing, rather than silently claiming a version it was not told.
 */
#ifndef JC_VERSION
#define JC_VERSION "unknown"
#endif

static void version(void)
{
    printf("\n");
    printf("    Johnny Reborn %s, an open-source engine for\n", JC_VERSION);
    printf("    the classic Johnny Castaway screensaver by Sierra.\n");
    printf("    Copyright (C) 2019 Jeremie GUILLAUME\n");
    printf("\n");
    jcExit(0);
}


#if defined(_WIN32)
/* Configuration stays independent of ZIP/resource loading. */
static INT_PTR CALLBACK configDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNUSED(lParam);
    if (message == WM_INITDIALOG) {
        struct TConfig cfg;
        cfgFileRead(&cfg);
        for (int i = 0; i < artStyleCount(); i++) {
            const TArtStyle *style = artStyleGet(i);
            LRESULT item = SendDlgItemMessageA(dialog, JC_STYLE_COMBO_ID, CB_ADDSTRING,
                                               0, (LPARAM)style->name);
            if (item == CB_ERR || item == CB_ERRSPACE) {
                EndDialog(dialog, -1);
                return TRUE;
            }
            SendDlgItemMessageA(dialog, JC_STYLE_COMBO_ID, CB_SETITEMDATA,
                                (WPARAM)item, i);
            if (!strcmp(style->id, cfg.artStyle))
                SendDlgItemMessageA(dialog, JC_STYLE_COMBO_ID, CB_SETCURSEL,
                                    (WPARAM)item, 0);
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK) {
            LRESULT item = SendDlgItemMessageA(dialog, JC_STYLE_COMBO_ID, CB_GETCURSEL, 0, 0);
            if (item == CB_ERR) return TRUE;
            LRESULT index = SendDlgItemMessageA(dialog, JC_STYLE_COMBO_ID, CB_GETITEMDATA,
                                                (WPARAM)item, 0);
            const TArtStyle *style = artStyleGet((int)index);
            if (!style) return TRUE;
            /* Read again so saving the dialog retains progress written since it opened. */
            struct TConfig cfg;
            cfgFileRead(&cfg);
            strcpy(cfg.artStyle, style->id);
            if (!cfgFileWrite(&cfg)) {
                MessageBoxA(dialog, "The art style could not be saved. Check that your profile folder is writable.",
                            "Johnny Reborn", MB_OK | MB_ICONERROR);
                return TRUE;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void showConfigDialog(void)
{
    HWND parent = (HWND)(uintptr_t)argParentHwnd;
    if (!IsWindow(parent)) parent = NULL;
    if (DialogBoxParamA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(JC_CONFIG_DIALOG_ID),
                        parent, configDialogProc, 0) == -1)
        fatalError("Could not open the art style settings dialog");
}
#endif


static void parseArgs(int argc, char **argv)
{
    typedef enum {
        EXPECT_NONE = 0,
        EXPECT_TTM_NAME,
        EXPECT_ADS_NAME,
        EXPECT_ADS_TAG,
        EXPECT_HOLIDAY,
        EXPECT_SEED,
        EXPECT_FRAMES,
        EXPECT_STYLE,
        EXPECT_SETSTYLE,
        EXPECT_CAPTURE,
        EXPECT_PARENT_HWND
    } TExpectedArg;

    TExpectedArg expect = EXPECT_NONE;

    for (int i=1; i < argc; i++) {

        if (expect != EXPECT_NONE) {
            switch (expect) {

                case EXPECT_TTM_NAME:
                    if (numArgs < MAX_ARGS)
                        args[numArgs++] = argv[i];
                    expect = EXPECT_NONE;
                    break;

                case EXPECT_ADS_NAME:
                    if (numArgs < MAX_ARGS)
                        args[numArgs++] = argv[i];
                    expect = EXPECT_ADS_TAG;
                    break;

                case EXPECT_ADS_TAG:
                    if (numArgs < MAX_ARGS)
                        args[numArgs++] = argv[i];
                    expect = EXPECT_NONE;
                    break;

                case EXPECT_HOLIDAY: {
                    int holiday = -1;
                    if (!parseHolidayToken(argv[i], &holiday))
                        fatalError("Unknown holiday '%s' (try: halloween, stpatricks, christmas, newyear, random, none, auto)", argv[i]);
                    if (holiday == -2)
                        holiday = pickRandomHoliday();
                    storySetForcedHoliday(holiday);
                    expect = EXPECT_NONE;
                    break;
                }

                /*  Numeric options REJECT trailing garbage, rather
                 *  than taking atoi's silent 0. `seed abc` naming a seed of 0
                 *  would run, look plausible, and quietly defeat the
                 *  reproducibility the option exists to provide.
                 */
                case EXPECT_SEED: {
                    char *end = NULL;
                    long v = strtol(argv[i], &end, 10);
                    if (end == argv[i] || (end && *end != '\0') || v < 0)
                        fatalError("Invalid seed '%s' (expected a non-negative integer)", argv[i]);
                    grForcedSeed = v;
                    expect = EXPECT_NONE;
                    break;
                }

                case EXPECT_FRAMES: {
                    char *end = NULL;
                    errno = 0;
                    long long v = strtoll(argv[i], &end, 10);
                    if (errno == ERANGE || end == argv[i] || *end != '\0' ||
                        v <= 0 || (unsigned long long)v > UINT32_MAX)
                        fatalError("Invalid frame count '%s' (expected an integer from 1 to 4294967295)", argv[i]);
                    evMaxFrames = (uint32)v;
                    expect = EXPECT_NONE;
                    break;
                }

                case EXPECT_STYLE:
                case EXPECT_SETSTYLE: {
                    const TArtStyle *style = artStyleFind(argv[i]);
                    if (!style) fatalError("Unknown art style '%s' (try: hd, cartoon)", argv[i]);
                    if (expect == EXPECT_STYLE) argStyle = style->id;
                    else argSetStyle = style->id;
                    expect = EXPECT_NONE;
                    break;
                }

                case EXPECT_CAPTURE:
                    if (!argv[i][0]) fatalError("Capture path cannot be empty");
                    grCapturePath = argv[i];
                    expect = EXPECT_NONE;
                    break;

                case EXPECT_PARENT_HWND: {
                    /*  The window handle after /p. Windows passes it in decimal,
                     *  but some shells and older documentation use hex, so base 0
                     *  accepts both rather than silently reading 0x1234 as 0.
                     *  A handle we cannot parse is fatal: rendering a preview
                     *  into the wrong window, or into none, is worse than saying
                     *  so.
                     */
                    char *end = NULL;
                    unsigned long long v = strtoull(argv[i], &end, 0);
                    if (end == argv[i] || (end && *end != '\0'))
                        fatalError("Invalid preview window handle '%s'", argv[i]);
                    argParentHwnd = v;
                    expect = EXPECT_NONE;
                    break;
                }

                default:
                    expect = EXPECT_NONE;
                    break;
            }
            continue;
        }

        if (!strcmp(argv[i], "help")) {
            usage();
        }
        else if (!strcmp(argv[i], "version")) {
            version();
        }
        else if (!strcmp(argv[i], "dump")) {
            argDump = 1;
        }
        else if (!strcmp(argv[i], "bench")) {
            argBench = 1;
        }
        else if (!strcmp(argv[i], "ttm")) {
            argTtm = 1;
            expect = EXPECT_TTM_NAME;
        }
        else if (!strcmp(argv[i], "ads")) {
            argAds = 1;
            expect = EXPECT_ADS_NAME;
        }
        else if (!strcmp(argv[i], "window")) {
            grWindowed = 1;
        }
        else if (isMinimizeArg(argv[i])) {
            argMinimize = 1;
        }
        else if (!strcmp(argv[i], "nosound")) {
            soundDisabled = 1;
        }
        else if (!strcmp(argv[i], "island")) {
            argIsland = 1;
        }
        else if (!strcmp(argv[i], "debug")) {
            debugMode = 1;
        }
        else if (!strcmp(argv[i], "hotkeys")) {
            evHotKeysEnabled = 1;
        }
        else if (!strcmp(argv[i], "maxspeed")) {
            evStartAtMaxSpeed = 1;
        }
        else if (!strcmp(argv[i], "night")) {
            storySetForcedNight(1);
        }
        else if (!strcmp(argv[i], "day")) {
            storySetForcedNight(0);
        }
        else if (!strcmp(argv[i], "seed")) {
            expect = EXPECT_SEED;
        }
        else if (!strcmp(argv[i], "frames")) {
            expect = EXPECT_FRAMES;
        }
        else if (!strcmp(argv[i], "style")) {
            expect = EXPECT_STYLE;
        }
        else if (!strcmp(argv[i], "setstyle")) {
            expect = EXPECT_SETSTYLE;
        }
        else if (!strcmp(argv[i], "capture")) {
            expect = EXPECT_CAPTURE;
        }
        else if (isHolidayArg(argv[i])) {
            expect = EXPECT_HOLIDAY;
        }
#if defined(_WIN32)
        /*  WINDOWS SCREENSAVER SWITCHES.
         *
         *  The shell runs a .scr as `/s` (run), `/c` or `/c:<hwnd>` (configure)
         *  and `/p <hwnd>` (preview in the little monitor). These reached the
         *  fallback below and were silently discarded, so ALL THREE ran the
         *  ordinary fullscreen screensaver. For /p that is the classic broken
         *  screensaver symptom: the preview pane launches a fullscreen window
         *  that steals the foreground while you are still in Settings.
         *
         *  Worse, normalizeToken strips non-alphanumerics, so a single-digit
         *  window handle after /p normalised to "4" and matched the numeric
         *  holiday shorthand.
         */
        else if (isScreensaverSwitch(argv[i], 's')) {
            argScreensaver = SCR_MODE_RUN;
        }
        else if (isScreensaverSwitch(argv[i], 'c')) {
            argScreensaver = SCR_MODE_CONFIG;
            /* /c:<hwnd> carries the parent inline; bare /c does not. */
            {
                const char *colon = strchr(argv[i], ':');
                if (colon && colon[1])
                    argParentHwnd = strtoull(colon + 1, NULL, 10);
            }
        }
        else if (isScreensaverSwitch(argv[i], 'p')) {
            argScreensaver = SCR_MODE_PREVIEW;
            expect = EXPECT_PARENT_HWND;
        }
        else if (isScreensaverSwitch(argv[i], 'a')) {
            /* Password change on very old Windows. Accepted and ignored so the
             * shell never sees an error, but it must not fall through to the
             * fallback and start playing. */
            argScreensaver = SCR_MODE_CONFIG;
        }
#endif
        else {
            // Shorthand: allow passing the holiday name directly
            // (e.g. `jc_reborn christmas`)
            int holiday = -1;
            if (parseHolidayToken(argv[i], &holiday)) {
                if (holiday == -2)
                    storySetForcedHoliday(pickRandomHoliday());
                else if (holiday >= 0)
                    storySetForcedHoliday(holiday);
            }
            else if (argv[i][0] == '/' || argv[i][0] == '-') {
                /*  REFUSE unknown switches instead of swallowing them. The old
                 *  code had no else at all here, so a typo, a flag from a newer
                 *  version, or anything the shell invented ran the default
                 *  screensaver and reported success. A wrong argument should not
                 *  look like a correct one.
                 */
                fatalError("Unknown option '%s' (try: jc_reborn help)", argv[i]);
            }
        }
    }

    if (expect != EXPECT_NONE)
        usage();

    if (argDump + argBench + argTtm + argAds + (argSetStyle != NULL) > 1)
        usage();

    if (argSetStyle && (argStyle || grCapturePath))
        fatalError("setstyle cannot be combined with style or capture");
    if (argDump && grCapturePath)
        fatalError("capture requires a graphical playback mode");

    if (argDump + argBench + argTtm + argAds == 0)
        argPlayAll = 1;
}


/*  JC_MAIN lets an embedding host rename the entry point (the macOS screen
 *  saver builds this file with -DJC_MAIN=jcEngineMain and calls it from a
 *  worker thread with a synthetic argv). A normal build keeps main(). */
#ifndef JC_MAIN
#define JC_MAIN main
#endif

int JC_MAIN(int argc, char **argv)
{
    parseArgs(argc, argv);

    if (argSetStyle) {
        struct TConfig cfg;
        cfgFileRead(&cfg);
        strcpy(cfg.artStyle, argSetStyle);
        if (!cfgFileWrite(&cfg)) return 1;
        printf("Saved art style: %s\n", cfg.artStyle);
        return 0;
    }

    if (!argDump) {
        struct TConfig cfg;
        cfgFileRead(&cfg);
        artStyleSelect(argStyle ? argStyle : cfg.artStyle);
    }

    if (argDump)
        debugMode = 1;

#if defined(_WIN32)
    /*  The configure switch answers and exits BEFORE the archive is opened.
     *
     *  Windows runs /c synchronously from the Screen Saver settings dialog and
     *  waits for it, so anything slow or fatal here freezes that dialog. Opening
     *  a 3.8 MB archive and parsing every resource to show a settings box would
     *  be both, and a missing archive would hang Settings rather than the
     *  screensaver.
     */
    if (argScreensaver == SCR_MODE_CONFIG) {
        showConfigDialog();
        return 0;
    }

    if (argScreensaver == SCR_MODE_RUN) {
        /*  Screensaver semantics: fullscreen, no console, and any real input
         *  ends it. grWindowed is already 0 by default, so this only has to turn
         *  on the input rules.
         */
        evScreensaverMode = 1;
        argPlayAll = 1;
    }
    else if (argScreensaver == SCR_MODE_PREVIEW) {
        /*  Preview draws into the tiny monitor in the Settings dialog. It must
         *  NOT go fullscreen, must not steal the foreground, and must exit when
         *  the shell destroys its parent window. Input is ignored here: the user
         *  is interacting with Settings, not with us.
         */
        grWindowed = 1;
        platformSetPreviewParent((void *)(uintptr_t)argParentHwnd);
        argPlayAll = 1;
        soundDisabled = 1;   /* a preview thumbnail that makes noise is a bug */
    }
#endif

    zipvfs_init("scrantic_data.zip");
    parseResourceFiles("data/RESOURCE.MAP");

    if (argPlayAll) {
        graphicsInit();

        if (argMinimize)
            minimizeConsoleWindow();

        soundInit();

        storyPlay();
        // NOTE: storyPlay() never returns (it loops forever and terminates
        // via exit() when input/quit events are received). Cleanup is handled
        // on the exit paths.
    }

    else if (argDump) {
        dumpAllResources();
    }

    else if (argBench) {
        graphicsInit();

        if (argMinimize)
            minimizeConsoleWindow();

        adsPlayBench();
        graphicsEnd();
    }

    else if (argTtm) {
        graphicsInit();

        if (argMinimize)
            minimizeConsoleWindow();

        soundInit();

        adsPlaySingleTtm(args[0]);

        soundEnd();
        graphicsEnd();
    }

    else if (argAds) {

        graphicsInit();

        if (argMinimize)
            minimizeConsoleWindow();

        soundInit();

        if (argIsland) {
            storyUpdateIslandFromDateAndTime();
            adsInitIsland();
        }
        else {
            adsNoIsland();
        }

        {
            char *end = NULL;
            long tagLong = strtol(args[1], &end, 10);

            if (end == args[1] || (end && *end != '\0') || tagLong < 0 || tagLong > 0xFFFFL) {
                fatalError("Invalid ADS tag '%s' (expected 0..65535)", args[1]);
            }

            adsPlay(args[0], (uint16)tagLong);
        }

        /*  PAIRED WITH adsInitIsland ABOVE. storyPlay() gets this right; this
         *  path did not, so the backdrop, holiday and cloud slots and their BMP
         *  surfaces were still held at graphicsEnd. Nothing accumulated across
         *  runs, because the process exits on the next line - but an init with
         *  no matching release is the kind of asymmetry that becomes a real leak
         *  the moment anything loops. Guarded on the same flag that created it.
         */
        if (argIsland)
            adsReleaseIsland();

        soundEnd();
        graphicsEnd();
    }

    zipvfs_shutdown();
    return 0;
}
