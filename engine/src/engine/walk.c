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

#include "platform.h"

#include "mytypes.h"
#include "graphics.h"
#include "utils.h"
#include "calcpath.h"
#include "walk.h"
#include "walk_data.h"
#include "art_style.h"
#include "zipvfs.h"
#include <stdio.h>


static int *walkPath;
static int currentSpot;
static int currentHdg;
static int nextSpot;
static int nextHdg;
static int finalSpot;
static int finalHdg;
static int increment;
static int lastTurn;
static int hasArrived;
static int isBehindTree;
static int palmUsesAtop;


void walkInit(int fromSpot, int fromHdg, int toSpot, int toHdg)
{
    const TArtStyle *style = artStyleCurrent();
    palmUsesAtop = 0;
    /* Partial packs without palm artwork retain the original HD path exactly. */
    if (!style->legacyColorKey) {
        char path[192];
        for (int image = 12; image <= 13; image++) {
            snprintf(path, sizeof(path), "%s/BMP/BACKGRND.BMP/%03d.png", style->root, image);
            if (zipvfs_exists(path)) palmUsesAtop = 1;
        }
    }
    walkPath = calcPath(fromSpot, toSpot);

    currentSpot  = fromSpot;
    currentHdg   = fromHdg;
    finalSpot    = toSpot;
    finalHdg     = toHdg;
    hasArrived   = 0;
    isBehindTree = 0;

    if (currentSpot == finalSpot) {
        nextSpot = -1;
        nextHdg = finalHdg;
        lastTurn = 1;
    }
    else {
        nextSpot = *(++walkPath);
        if (nextSpot < 0 || nextSpot >= NUM_OF_NODES) {
            debugMsg("WALKING: invalid nextSpot %d", nextSpot);
            hasArrived = 1;
            nextHdg = fromHdg;
            return;
        }
        nextHdg = walkDataStartHeadings[currentSpot][nextSpot];
        lastTurn = 0;
    }

    increment = ((nextHdg - currentHdg) & 0x07);
    if (increment != 0) {
        increment = (increment < 4 ? 1 : -1);
    }
}


uint16 walkAnimate(struct TTtmThread *ttmThread, struct TTtmSlot *ttmBgSlot)
{
    struct TTtmSlot *ttmSlot = ttmThread->ttmSlot;
    PlatformSurface *sfc = ttmThread->ttmLayer;
    static uint16 (*data)[4] = NULL;
    uint16 delay;

    if (!hasArrived) {

        // Are we turning ?
        if (nextHdg != -1) {

            // More than one iteration left? yes, so let's turn
            if ((((nextHdg - currentHdg) & 0x07) ) > 1) {
                currentHdg = (currentHdg + increment ) & 7;
                data = &walkData[walkDataBookmarksTurns[currentSpot] + currentHdg];
                if (lastTurn)
                    data += 9;
            }

            // The turn is over
            else {

                // Do we have another spot to walk to ?
                if (currentSpot != finalSpot) {
                    nextHdg = -1;
                    isBehindTree = ((currentSpot == 3) && (nextSpot == 4))
                                      || ((currentSpot == 4) && (nextSpot == 3));
                    if (walkDataBookmarks[currentSpot][nextSpot] < 0) {
                        debugMsg("WALKING: invalid bookmark for spot %d -> %d", currentSpot, nextSpot);
                        hasArrived = 1;
                        data = &walkData[walkDataBookmarksTurns[currentSpot] + currentHdg];
                    }
                    else
                        data = &walkData[walkDataBookmarks[currentSpot][nextSpot]];
                }

                // Else, we arrived to destination
                else {
                    data = &walkData[walkDataBookmarksTurns[finalSpot] + finalHdg];
                    data += 9;     // hands in pockets
                    hasArrived = 1;
                }
            }
        }

        // Walking forward
        else {

            data++;

            // Have we reached a spot ? So lets begin a turn...
            if (!(*data)[1]) {

                currentHdg = walkDataEndHeadings[currentSpot][nextSpot];
                currentSpot = nextSpot;

                // What's the next heading ?
                // And the next spot of the path to reach ?
                if (currentSpot != finalSpot) {
                    nextSpot = *(++walkPath);
                    nextHdg = walkDataStartHeadings[currentSpot][nextSpot];
                }
                else {
                    nextHdg = finalHdg;
                    lastTurn = 1;
                }

                // Turning: left or right ?
                increment = ((nextHdg - currentHdg) & 0x07);
                if (increment != 0) {
                    increment = (increment < 4 ? 1 : -1);
                }

                currentHdg = (currentHdg + increment) & 7;
                data = &walkData[walkDataBookmarksTurns[currentSpot] + currentHdg];

                if (lastTurn) {
                    data += 9;   // hands in pockets
                    if (currentHdg == finalHdg)
                        hasArrived = 1;
                }
            }
        }

        debugMsg("WALKING:  spot=%d hdg=%d next=%d  -  data %d %d %d %d\n",
            currentSpot, currentHdg, nextHdg,
            (*data)[0], (*data)[1], (*data)[2], (*data)[3]);

        grClearScreen(sfc);

        if ((*data)[0])
            grDrawSpriteFlip(sfc, ttmSlot,
                (sint16)((*data)[1] - 1),
                (sint16)(*data)[2],
                (*data)[3],
                0);
        else
            grDrawSprite(sfc, ttmSlot,
                (sint16)((*data)[1] - 1),
                (sint16)(*data)[2],
                (*data)[3],
                0);

        if (isBehindTree) {
            if (palmUsesAtop) {
                grDrawSpriteAtop(sfc, ttmBgSlot, 442, 148, 13, 0);  // trunk
                grDrawSpriteAtop(sfc, ttmBgSlot, 365, 122, 12, 0);  // leafs
            }
            else {
                grDrawSprite(sfc, ttmBgSlot, 442, 148, 13, 0);  // trunk
                grDrawSprite(sfc, ttmBgSlot, 365, 122, 12, 0);  // leafs
            }
        }

        if (hasArrived)
            delay = 80;
        else
            delay = 6;
    }
    else {
        debugMsg("WALKING: end walk\n");
        delay = 0;
    }

    return delay;
}

