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

#include "mytypes.h"
#include "graphics.h"
#include "island.h"
#include "utils.h"
#include "art_style.h"
#include "zipvfs.h"


struct TIslandState islandState = {0};

struct TWaveFamily {
    int x, y, firstSprite;
};

static const struct TWaveFamily highWaves[] = {
    {270, 306, 3}, {364, 319, 6}, {518, 303, 9}
};
static const struct TWaveFamily lowWaves[] = {
    {129, 340, 39}, {233, 323, 30}, {367, 356, 33}, {558, 323, 36}
};

static uint8 *waveBase = NULL;
static PlatformRect waveBounds;
static int waveSprites[4];
static int waveOrder[4];
static int waveOrderCount = 0;

void islandRelease(void)
{
    free(waveBase);
    waveBase = NULL;
    waveOrderCount = 0;
}

static void islandSaveWaveBase(struct TTtmSlot *slot)
{
    const TArtStyle *style = artStyleCurrent();
    const struct TWaveFamily *families = islandState.lowTide ? lowWaves : highWaves;
    int count = islandState.lowTide ? 4 : 3;
    int hasReplacement = 0;
    int left = grRenderWidth, top = grRenderHeight, right = 0, bottom = 0;

    /* HD's opaque stamping is retained exactly, including in partial packs
     * that have no selected-style replacement for the active tide's waves. */
    if (style->legacyColorKey) return;
    for (int family = 0; family < count; family++) {
        int x = (families[family].x + islandState.xPos) * grScale;
        int y = (families[family].y + islandState.yPos) * grScale;
        for (int phase = 0; phase < 3; phase++) {
            int image = families[family].firstSprite + phase;
            char path[192];
            snprintf(path, sizeof(path), "%s/BMP/BACKGRND.BMP/%03d.png", style->root, image);
            if (zipvfs_exists(path)) hasReplacement = 1;
            if (image < slot->numSprites[0]) {
                PlatformSurface *sprite = slot->sprites[0][image];
                int dx, dy;
                artStyleSpriteOffset("BACKGRND.BMP", image,
                    platformGetSurfaceWidth(sprite), platformGetSurfaceHeight(sprite), 0, &dx, &dy);
                int spriteLeft = x + dx, spriteTop = y + dy;
                int spriteRight = spriteLeft + platformGetSurfaceWidth(sprite);
                int spriteBottom = spriteTop + platformGetSurfaceHeight(sprite);
                if (spriteLeft < left) left = spriteLeft;
                if (spriteTop < top) top = spriteTop;
                if (spriteRight > right) right = spriteRight;
                if (spriteBottom > bottom) bottom = spriteBottom;
            }
        }
    }
    if (!hasReplacement) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > platformGetSurfaceWidth(grBackgroundSfc)) right = platformGetSurfaceWidth(grBackgroundSfc);
    if (bottom > platformGetSurfaceHeight(grBackgroundSfc)) bottom = platformGetSurfaceHeight(grBackgroundSfc);
    if (right <= left || bottom <= top) return;
    waveBounds = (PlatformRect){left, top, right - left, bottom - top};
    size_t rowBytes = (size_t)waveBounds.w * 4;
    waveBase = safe_malloc(rowBytes * (size_t)waveBounds.h);
    platformLockSurface(grBackgroundSfc);
    const uint8 *pixels = platformGetSurfacePixels(grBackgroundSfc);
    int pitch = platformGetSurfacePitch(grBackgroundSfc);
    for (int y = 0; y < waveBounds.h; y++) {
        memcpy(waveBase + (size_t)y * rowBytes,
               pixels + (size_t)(waveBounds.y + y) * pitch + (size_t)waveBounds.x * 4,
               rowBytes);
    }
    platformUnlockSurface(grBackgroundSfc);
}

static void islandDrawWave(struct TTtmSlot *slot, int family, int phase)
{
    const struct TWaveFamily *families = islandState.lowTide ? lowWaves : highWaves;
    if (!waveBase) {
        grDrawSprite(grBackgroundSfc, slot, families[family].x, families[family].y,
                     (uint16)(families[family].firstSprite + phase), 0);
        return;
    }

    /* An updated family becomes the topmost one, as in the original stamping
     * order. Restoring only its rectangle would erase overlapping neighbors. */
    int position = 0;
    while (position < waveOrderCount && waveOrder[position] != family) position++;
    if (position == waveOrderCount) waveOrderCount++;
    for (int i = position; i + 1 < waveOrderCount; i++) waveOrder[i] = waveOrder[i + 1];
    waveOrder[waveOrderCount - 1] = family;
    waveSprites[family] = families[family].firstSprite + phase;

    size_t rowBytes = (size_t)waveBounds.w * 4;
    platformLockSurface(grBackgroundSfc);
    uint8 *pixels = platformGetSurfacePixels(grBackgroundSfc);
    int pitch = platformGetSurfacePitch(grBackgroundSfc);
    for (int y = 0; y < waveBounds.h; y++) {
        memcpy(pixels + (size_t)(waveBounds.y + y) * pitch + (size_t)waveBounds.x * 4,
               waveBase + (size_t)y * rowBytes, rowBytes);
    }
    platformUnlockSurface(grBackgroundSfc);
    for (int i = 0; i < waveOrderCount; i++) {
        int current = waveOrder[i];
        grDrawSprite(grBackgroundSfc, slot, families[current].x, families[current].y,
                     (uint16)waveSprites[current], 0);
    }
}


void islandInit(struct TTtmThread *ttmThread)
{
    struct TTtmSlot *ttmSlot = ttmThread->ttmSlot;
    islandRelease();

    /*  The backdrop choice is logged because it is otherwise unobservable from
     *  outside: night depends on the wall clock (21:00-05:59), so a daytime test
     *  run and a broken night path are indistinguishable without it. The smoke
     *  suite asserts on this line.
     */
    if (islandState.night) {
        grLoadScreen("NIGHT.SCR");
        debugMsg("island backdrop: NIGHT.SCR");
    }
    else {
        char scrName[16];
        snprintf(scrName, sizeof(scrName), "OCEAN0%d.SCR", rand() % 3);
        grLoadScreen(scrName);
        debugMsg("island backdrop: %s", scrName);
    }

    ttmThread->ttmLayer = grBackgroundSfc;

    grDx = islandState.xPos;
    grDy = islandState.yPos;


    // Raft

    grLoadBmp(ttmSlot, 0, "MRAFT.BMP");

    sint16 xRaft = (sint16)(islandState.lowTide ? 529 : 512);
    sint16 yRaft = (sint16)(islandState.lowTide ? 281 : 266);

    switch (islandState.raft) {
        case 1: grDrawSprite(grBackgroundSfc, ttmSlot, xRaft, yRaft, 0, 0); break;  // raft-1
        case 2: grDrawSprite(grBackgroundSfc, ttmSlot, xRaft, yRaft, 1, 0); break;  // raft-2
        case 3: grDrawSprite(grBackgroundSfc, ttmSlot, xRaft, yRaft, 2, 0); break;  // raft-3
        case 4: grDrawSprite(grBackgroundSfc, ttmSlot, xRaft, yRaft, 3, 0); break;  // raft-4
        case 5: grDrawSprite(grBackgroundSfc, ttmSlot, xRaft, yRaft, 4, 0); break;  // raft-5
    }


    grLoadBmp(ttmSlot, 0, "BACKGRND.BMP");


    // Clouds

    grDx = grDy = 0;

    sint32 cloudX = 0;
    sint32 cloudY = 0;

    sint32 numClouds = rand() % 6;
    sint32 windDirection = rand() % 2;

    islandState.clouds.numClouds = numClouds;
    islandState.clouds.windDirection = windDirection;

    for (sint32 i=0; i < numClouds; i++) {
        sint32 cloudNo = rand() % 3;
        switch (cloudNo) {
            case 0:
                cloudX = rand() % (SCREEN_WIDTH - 129);
                cloudY = rand() % (100 - 36 ) + 25;
                break;

            case 1:
                cloudX = rand() % (SCREEN_WIDTH - 192);
                cloudY = rand() % (100 - 57 ) + 25;
                break;

            case 2:
                cloudX = rand() % (SCREEN_WIDTH - 264);
                cloudY = rand() % (100 - 76 ) + 25;
                break;

            default:
                cloudX = 0;
                cloudY = 0;
                break;
        }
        islandState.clouds.windSpeed[i] = rand() % 2 + 1;
        islandState.clouds.cloudNo[i] = cloudNo;
        islandState.clouds.xPos[i] = cloudX;
        islandState.clouds.yPos[i] = cloudY;
    }

    grDx = islandState.xPos;
    grDy = islandState.yPos;

    // The island itself

    grDrawSprite(grBackgroundSfc, ttmSlot, 288, 279,  0, 0);      // island
    grDrawSprite(grBackgroundSfc, ttmSlot, 442, 148, 13, 0);      // trunk
    grDrawSprite(grBackgroundSfc, ttmSlot, 365, 122, 12, 0);      // leafs
    grDrawSprite(grBackgroundSfc, ttmSlot, 396, 279, 14, 0);      // palmtree's shadow

    if (islandState.lowTide) {
        grDrawSprite(grBackgroundSfc, ttmSlot, 249, 303,  1, 0);  // low tide shore
        grDrawSprite(grBackgroundSfc, ttmSlot, 150, 328,  2, 0);  // rock
    }

    // Keep the static island separate from true-alpha wave animation.
    islandSaveWaveBase(ttmSlot);

    // Initial waves on the shore
    for (int i=0; i < 4; i++) {
        islandAnimate(ttmThread);
    }

    // Waves animation thread
    ttmThread->delay = ttmThread->timer = 8;
}

void islandAnimate(struct TTtmThread *ttmThread)
{
    static sint32 counter1 = 0;
    static sint32 counter2 = 0;

    struct TTtmSlot *ttmSlot = ttmThread->ttmSlot;

    grDx = islandState.xPos;
    grDy = islandState.yPos;

    counter2++;
    counter2 %= islandState.lowTide ? 4 : 3;
    islandDrawWave(ttmSlot, counter2, counter1);

    if (!counter2) {
        counter1++;
        counter1 %= 3;
    }
}

/**
 * islandInitHoliday()
 *
 * Configures island decorations for the given holiday mode.
 * Parameters: ttmThread.

 */
void islandInitHoliday(struct TTtmThread *ttmThread) {
    struct TTtmSlot *ttmSlot = ttmThread->ttmSlot;

    if (islandState.holiday) {
        ttmThread->ttmLayer  = grNewLayer();
        ttmThread->isRunning = TTM_STATIC_LAYER;

        grDx = islandState.xPos;
        grDy = islandState.yPos;

        grLoadBmp(ttmSlot, 0, "HOLIDAY.BMP");

        switch (islandState.holiday) {
            case 1: grDrawSprite(ttmThread->ttmLayer, ttmSlot, 410, 298, 0, 0); break;   // Halloween
            case 2: grDrawSprite(ttmThread->ttmLayer, ttmSlot, 333, 286, 1, 0); break;   // St Patrick
            case 3: grDrawSprite(ttmThread->ttmLayer, ttmSlot, 404, 267, 2, 0); break;   // Christmas
            case 4: grDrawSprite(ttmThread->ttmLayer, ttmSlot, 361, 155, 3, 0); break;   // New year
        }

        grReleaseBmp(ttmSlot,0);
    }
    else {
        ttmThread->isRunning = TTM_FREE;
    }
}

/**
 * islandAnimateClouds()
 *
 * Updates and draws moving clouds over the island.
 * Parameters: ttmThread.

 */
void islandAnimateClouds(struct TTtmThread *ttmThread) {
    struct TTtmSlot *ttmSlot = ttmThread->ttmSlot;
    grClearScreen(ttmThread->ttmLayer);
    if (islandState.clouds.numClouds > 0) {
        ttmThread->isRunning = TTM_STATIC_LAYER;
        grLoadBmp(ttmSlot, 0, "BACKGRND.BMP");

        // animate clouds x position
        for (sint32 i=0; i < islandState.clouds.numClouds; i++) {
            sint32 cloudNo = islandState.clouds.cloudNo[i];
            sint32 cloudX = islandState.clouds.xPos[i];
            sint32 cloudY = islandState.clouds.yPos[i];

            if (cloudX > SCREEN_WIDTH + 264) {
                cloudX = -264;
            } else if (cloudX < -264) {
                cloudX = SCREEN_WIDTH + 264;
            }
            else {
                if (islandState.clouds.windDirection) {
                    cloudX -= islandState.clouds.windSpeed[i];
                } else {
                    cloudX += islandState.clouds.windSpeed[i];
                }
            }

            debugMsg("Clouds Pos: %d, %d", cloudX, cloudY);
            if (islandState.clouds.windDirection) {
                grDrawSprite(ttmThread->ttmLayer,
                             ttmSlot,
                             (sint16)cloudX,
                             (sint16)cloudY,
                             (uint16)(15 + cloudNo),
                             0);
            } else {
                grDrawSpriteFlip(ttmThread->ttmLayer,
                                 ttmSlot,
                                 (sint16)cloudX,
                                 (sint16)cloudY,
                                 (uint16)(15 + cloudNo),
                                 0);
            }

            islandState.clouds.xPos[i] = cloudX;
            islandState.clouds.yPos[i] = cloudY;
        }
    } else {
        ttmThread->isRunning = TTM_FREE;
    }
}
