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

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "platform.h"
#include "resource.h"

#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT       480

#define MAX_BMP_SLOTS       6
#define MAX_SPRITES_PER_BMP 120
#define MAX_TTM_SLOTS       10
#define MAX_TTM_THREADS     10


struct TAdsScene {
    uint16 slot;
    uint16 tag;
};


struct TTtmSlot {
    uint8       *data;
    uint32      dataSize;
    struct      TTtmTag *tags;
    int         numTags;
    int         numSprites[MAX_BMP_SLOTS];
    const char  *bmpNames[MAX_BMP_SLOTS];
    PlatformSurface *sprites[MAX_BMP_SLOTS][MAX_SPRITES_PER_BMP];
};

struct TTtmTag {  // TODO : rename, used for ADS too
    uint16 id;
    uint32 offset;
};

/*
 * TTM thread lifecycle state. See CHANGELOG.md 2026-04-20 for the confirmed
 * transition table. Truthy check (if (thread.isRunning)) treats everything
 * except TTM_FREE as "active" — this is deliberate and used in compositing
 * and tick dispatch.
 */
typedef enum {
    TTM_FREE         = 0,  /* slot available; not composited, not ticked  */
    TTM_RUNNING      = 1,  /* executing bytecode via ttmPlay each frame   */
    TTM_ENDING       = 2,  /* transient: resolved same frame → RUNNING or FREE */
    TTM_STATIC_LAYER = 3   /* background/clouds/holiday: composited only, no bytecode */
} TtmRunState;

struct TTtmThread {
    struct TTtmSlot   *ttmSlot;
    TtmRunState isRunning;
    uint16 sceneSlot;
    uint16 sceneTag;
    short  sceneTimer;
    uint16 sceneIterations;
    uint32 ip;
    uint16 delay;
    uint16 timer;
    uint32 nextGotoOffset;
    uint8  selectedBmpSlot;
    uint8  fgColor;
    uint8  bgColor;
    PlatformSurface *ttmLayer;
};

extern PlatformSurface *grBackgroundSfc;

extern int grDx;
extern int grDy;
extern int grWindowed;

/* Forced RNG seed set by `seed <N>`, or -1 for the default clock-derived seed.
 * Every random choice in the engine comes off this one stream, so fixing it is
 * what makes the story and island paths reproducible for testing. */
extern long grForcedSeed;
extern uint16 grUpdateDelay;

// HD / scaling support
extern int grScale;
extern int grRenderWidth;
extern int grRenderHeight;
extern int grHdEnabled;
/* Optional P6 capture of the composed render surface on normal graphics exit. */
extern const char *grCapturePath;


void graphicsInit(void);
void graphicsEnd(void);
void grRefreshDisplay(void);
void grToggleFullScreen(void);
void grUpdateDisplay(struct TTtmThread *ttmThreads,
                     struct TTtmThread *ttmHolidayThreads,
                     struct TTtmThread *ttmCloudThreads);

void grInitEmptyBackground(void);
PlatformSurface *grNewLayer(void);
void grFreeLayer(PlatformSurface *sfc);
/* Release the owned overlay without implying bytecode RESTORE_ZONE semantics. */
void grReleaseSavedLayer(void);

void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, const char *strArg);
void grReleaseBmp(struct TTtmSlot *ttmSlot, uint16 bmpSlotNo);

void grSetClipZone(PlatformSurface *sfc, int x1, int y1, int x2, int y2);
void grCopyZoneToBg(PlatformSurface *sfc, int x, int y, int width, int height);
void grSaveImage1(PlatformSurface *sfc, int x, int y, int width, int height);
void grSaveZone(PlatformSurface *sfc, int x, int y, int width, int height);
void grRestoreZone(PlatformSurface *sfc, int x, int y, int width, int height);
void grDrawPixel(PlatformSurface *sfc, int x, int y, uint8 color);
void grDrawLine(PlatformSurface *sfc, int x1, int y1, int x2, int y2, uint8 color);
void grDrawRect(PlatformSurface *sfc, int x, int y, int width, int height, uint8 color);
void grDrawCircle(PlatformSurface *sfc, int x1, int y1, int width, int height, uint8 fgColor, uint8 bgColor);
void grDrawSprite(PlatformSurface *sfc, struct TTtmSlot *ttmSlot, int x, int y, uint16 spriteNo, uint16 imageNo);
/* Premultiplied source-atop: recolor occupied destination pixels while retaining
 * destination alpha. Used when the same palm is already in the background. */
void grDrawSpriteAtop(PlatformSurface *sfc, struct TTtmSlot *ttmSlot, int x, int y, uint16 spriteNo, uint16 imageNo);
void grDrawSpriteFlip(PlatformSurface *sfc, struct TTtmSlot *ttmSlot, int x, int y, uint16 spriteNo, uint16 imageNo);
void grClearScreen(PlatformSurface *sfc);
void grFadeOut(void);

void grLoadPalette(struct TPalResource *palResource);
void grLoadScreen(const char *strArg);

#endif /* GRAPHICS_H */

