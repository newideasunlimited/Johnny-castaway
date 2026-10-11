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
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "platform.h"

#include "mytypes.h"
#include "utils.h"
#include "graphics.h"
#include "resource.h"
#include "events.h"
#include "art_style.h"
#include "island.h"


static PlatformWindow *platform_window;

static uint8 ttmPalette[16][4];

static PlatformSurface *grSavedZonesLayer = NULL;

static PlatformRect grScreenOrigin = { 0, 0, 0, 0 };   // TODO

PlatformSurface *grBackgroundSfc = NULL;

int grDx = 0;
int grDy = 0;
int grWindowed = 0;

/* Forced RNG seed, or -1 to derive one from the clock. See graphicsInit(). */
long grForcedSeed = -1;
uint16 grUpdateDelay = 0;

// HD / scaling support (logical coordinate space stays 640x480)
int grScale = 1;                 // 1 = original, 2 = 2x, etc.
int grRenderWidth = SCREEN_WIDTH;
int grRenderHeight = SCREEN_HEIGHT;
int grHdEnabled = 0;
const char *grCapturePath = NULL;


static void grReleaseScreen(void)
{
    islandRelease();
    free(platformGetSurfacePixels(grBackgroundSfc));
    platformFreeSurface(grBackgroundSfc);
    grBackgroundSfc = NULL;
}


void grReleaseSavedLayer(void)
{
    platformFreeSurface(grSavedZonesLayer);
    grSavedZonesLayer = NULL;
}



static void grPutPixel(PlatformSurface *sfc, int x, int y, uint8 color)
{
    // TODO: Implement Cohen-Sutherland clipping algorithm or such for
    // grDrawLine(), and another ad hoc algorithm for grDrawCircle()

    // Coordinates are in *logical* pixels (640x480). We write a grScale x grScale
    // block of real pixels to the render surface.
    if (x>=0 && y>=0 && x<SCREEN_WIDTH && y<SCREEN_HEIGHT && color < 16) {

        uint8 *base = platformGetSurfacePixels(sfc);
        int pitch = platformGetSurfacePitch(sfc);
        int bpp = platformGetSurfaceBytesPerPixel(sfc);

        int rx = x * grScale;
        int ry = y * grScale;

        for (int dy=0; dy < grScale; dy++) {
            uint8 *row = base + ((size_t)(ry + dy) * (size_t)pitch) + ((size_t)rx * (size_t)bpp);
            for (int dx=0; dx < grScale; dx++) {
                uint8 *pixel = row + ((size_t)dx * (size_t)bpp);
                pixel[0] = ttmPalette[color][0];
                pixel[1] = ttmPalette[color][1];
                pixel[2] = ttmPalette[color][2];
                pixel[3] = 255;
            }
        }
    }
}


static void grDrawHorizontalLine(PlatformSurface *sfc, int x1, int x2, int y, uint8 color)
{
    if (y < 0 || y > (SCREEN_HEIGHT - 1))
        return;

    x1 = x1 < 0   ? 0   : x1;
    x2 = x2 > (SCREEN_WIDTH - 1) ? (SCREEN_WIDTH - 1) : x2;

    for (int x=x1; x<=x2; x++)
        grPutPixel(sfc, x, y, color);
}


void grLoadPalette(struct TPalResource *palResource)
{
    if (palResource == NULL)
        fatalError("NULL palette\n");

    for (int i=0; i < 16; i++) {
        ttmPalette[i][0] = (uint8)(palResource->colors[i].b << 2);
        ttmPalette[i][1] = (uint8)(palResource->colors[i].g << 2);
        ttmPalette[i][2] = (uint8)(palResource->colors[i].r << 2);
        ttmPalette[i][3] = 255;
    }
}



static void grDetectArtAssets(void)
{
    char error[768];
    if (!artStyleValidatePack(error, sizeof(error)))
        fatalError("%s", error);
    grScale = artStyleCurrentScale();
    grHdEnabled = grScale > 1;

    grRenderWidth = SCREEN_WIDTH * grScale;
    grRenderHeight = SCREEN_HEIGHT * grScale;

    if (debugMode && grHdEnabled && artStyleCurrent()->legacyColorKey) {
        printf("HD assets enabled (scale=%d, render=%dx%d)\n",
               grScale, grRenderWidth, grRenderHeight);
    }
}


void graphicsInit(void)
{
    grDetectArtAssets();
    platformInit();

    platform_window = platformCreateWindow(
        "Johnny Reborn ...?",
        grRenderWidth,
        grRenderHeight,
        (grWindowed ? 0 : 1)
    );

    if (platform_window == NULL)
        fatalError("Could not create window: %s", platformGetError());

    grScreenOrigin.x = 0;
    grScreenOrigin.y = 0;
    grScreenOrigin.w = grRenderWidth;
    grScreenOrigin.h = grRenderHeight;

    if (!grWindowed)
        platformShowCursor(0);

    platformUpdateWindow(platform_window);

    grLoadPalette(palResources[0]);  // TODO ?

    {
        unsigned int seed;

        /*  A FIXED SEED MAKES THE ENGINE TESTABLE. Scene selection
         *  (story.c:79), cloud count and placement (island.c:82-111), ocean
         *  backdrop choice (island.c:46), low-tide rolls (story.c:150) and path
         *  choice (calcpath.c:127) all come off this one stream, so without
         *  `seed <N>` two runs of the same command never agree and nothing
         *  about the island path can be asserted.
         *
         *  Default stays time-derived: a screensaver that picked the same
         *  scenes every boot would be a regression in its own right.
         */
        if (grForcedSeed >= 0) {
            seed = (unsigned int)grForcedSeed;
        }
        else {
            // srand() takes an unsigned int; time_t may be 64-bit.
            // Fold the time value down to 32 bits in a deterministic way.
            time_t now = time(NULL);
            unsigned long long t = (unsigned long long)now;
            seed = (unsigned int)(t ^ (t >> 32));
        }

        srand(seed);
        debugMsg("rand seed: %u%s", seed, grForcedSeed >= 0 ? " (forced)" : "");
    }

    eventsInit();
}


static void grCaptureFrame(void)
{
    if (!grCapturePath) return;
    PlatformSurface *surface = platformGetWindowSurface(platform_window);
    int width = platformGetSurfaceWidth(surface);
    int height = platformGetSurfaceHeight(surface);
    int pitch = platformGetSurfacePitch(surface);
    uint8 *pixels = platformGetSurfacePixels(surface);
    if (!pixels || width <= 0 || height <= 0 || platformGetSurfaceBytesPerPixel(surface) != 4)
        fatalError("Cannot capture frame to %s: invalid render surface", grCapturePath);
    FILE *file = fopen(grCapturePath, "wb");
    if (!file) fatalError("Cannot open frame capture %s", grCapturePath);
    uint8 *row = safe_malloc((size_t)width * 3);
    int ok = fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
    for (int y = 0; y < height && ok; y++) {
        const uint8 *source = pixels + (size_t)y * (size_t)pitch;
        for (int x = 0; x < width; x++) {
            row[(size_t)x * 3] = source[(size_t)x * 4 + 2];
            row[(size_t)x * 3 + 1] = source[(size_t)x * 4 + 1];
            row[(size_t)x * 3 + 2] = source[(size_t)x * 4];
        }
        ok = fwrite(row, 3, (size_t)width, file) == (size_t)width;
    }
    free(row);
    if (fclose(file) != 0) ok = 0;
    if (!ok) fatalError("Cannot write frame capture %s", grCapturePath);
    printf("Captured frame: %s (%dx%d)\n", grCapturePath, width, height);
}

void graphicsEnd(void)
{
    /* Capture before releasing graphics owners. Platform shutdown stays with
     * eventsInit's atexit registration, which also covers fatalError exits. */
    if (platform_window) grCaptureFrame();
    grReleaseSavedLayer();
    grReleaseScreen();
    artStyleReportUsage();
    platformDestroyWindow(platform_window);
    platform_window = NULL;
}


void grRefreshDisplay(void)
{
    platformUpdateWindow(platform_window);
}


void grToggleFullScreen(void)
{
    grWindowed = !grWindowed;

    platformToggleFullscreen(platform_window);

    if (grWindowed) {
        platformShowCursor(1);
    }
    else {
        platformShowCursor(0);
    }

    platformUpdateWindow(platform_window);
}


void grUpdateDisplay(struct TTtmThread *ttmThreads,
                     struct TTtmThread *ttmHolidayThread,
                     struct TTtmThread *ttmCloudsThread)
{
    PlatformSurface* windowSurface = platformGetWindowSurface(platform_window);

    // Blit the background
    if (grBackgroundSfc != NULL)
        platformBlitSurface(grBackgroundSfc,
                        NULL,
                        windowSurface,
                        &grScreenOrigin);

    // Blit the Clouds
    if (ttmCloudsThread != NULL)
        if (ttmCloudsThread->isRunning)
            platformBlitSurface(ttmCloudsThread->ttmLayer,
                            NULL,
                            windowSurface,
                            &grScreenOrigin);

    // If not NULL, blit the optional layer of saved zones
    if (grSavedZonesLayer != NULL)
        platformBlitSurface(grSavedZonesLayer,
                        NULL,
                        windowSurface,
                        &grScreenOrigin);


    // Blit successively each thread's layer
    for (int i=0; i < MAX_TTM_THREADS; i++)
        if (ttmThreads[i].isRunning)
            platformBlitSurface(ttmThreads[i].ttmLayer,
                            NULL,
                            windowSurface,
                            &grScreenOrigin);

    // Finally, blit the holiday layer
    if (ttmHolidayThread != NULL)
        if (ttmHolidayThread->isRunning)
            platformBlitSurface(ttmHolidayThread->ttmLayer,
                            NULL,
                            windowSurface,
                            &grScreenOrigin);

    // Wait for the tick ...
    eventsWaitTick(grUpdateDelay);

    // ... and refresh the display
    platformUpdateWindow(platform_window);
}
PlatformSurface *grNewLayer(void)
{
    // Layers are drawn over the background, so they need real transparency.
    // We use per-pixel alpha (premultiplied BGRA / PBGRA), not a magenta color-key.
    PlatformSurface *sfc = platformCreateSurface(grRenderWidth, grRenderHeight);
    if (!sfc) fatalError("Could not create drawing layer: %s", platformGetError());
    PlatformRect dest = { 0, 0, grRenderWidth, grRenderHeight };
    platformFillRect(sfc, &dest, 0, 0, 0, 0);  // fully transparent
    return sfc;
}



void grFreeLayer(PlatformSurface *sfc)
{
    platformFreeSurface(sfc);
}


void grSetClipZone(PlatformSurface *sfc, int x1, int y1, int x2, int y2)
{
    x1 += grDx; y1 += grDy;
    x2 += grDx; y2 += grDy;

    int w = x2 - x1;
    int h = y2 - y1;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    PlatformRect rect = { x1 * grScale, y1 * grScale, w * grScale, h * grScale };
    platformSetClipRect(sfc, &rect);
}


void grCopyZoneToBg(PlatformSurface *sfc, int x, int y, int width, int height)
{
    x += grDx; y += grDy;
    PlatformRect rect = { x * grScale, y * grScale, (width + 2) * grScale, height * grScale };

    if (grSavedZonesLayer == NULL)
        grSavedZonesLayer = grNewLayer();

    platformBlitSurface(sfc, &rect, grSavedZonesLayer, &rect);

    // Note : without the +2 in width+2 above, there would be a graphical
    // glitch (2 unfilled pixels) on the hull of the cargo, caused by an
    // error in coordinates in GJIVS6.TTM
    // Obviously, the original soft rounds the SAVE_IMAGE boundaries on
    // one way or another.
}


void grSaveImage1(PlatformSurface *sfc, int x, int y, int width, int height) // TODO : rename ?
{
    UNUSED(sfc);
    UNUSED(x);
    UNUSED(y);
    UNUSED(width);
    UNUSED(height);
//    ttmSetColors(4,4);
//    ttmDrawRect(arg0,arg1,arg2,arg3);
//    ttmSaveImage0(arg0,arg1,arg2,arg3);
//    ttmUpdate();
}


void grSaveZone(PlatformSurface *sfc, int x, int y, int width, int height)
{
    UNUSED(sfc);
    UNUSED(x);
    UNUSED(y);
    UNUSED(width);
    UNUSED(height);
    // Minimalistic implementation: we don't really save the zone,
    // and let grRestoreZone() simply erase the 'saved zones' layer
}


void grRestoreZone(PlatformSurface *sfc, int x, int y, int width, int height)
{
    UNUSED(sfc);
    UNUSED(x);
    UNUSED(y);
    UNUSED(width);
    UNUSED(height);
    // In Johnny's TTMs, we never have RESTORE_ZONE called
    // while several zones are saved. So we simply free the
    // whole saved zones layer
    grReleaseSavedLayer();
}


void grDrawPixel(PlatformSurface *sfc, int x, int y, uint8 color)
{
    x += grDx; y += grDy;
    grPutPixel(sfc, x, y, color);
}


void grDrawLine(PlatformSurface *sfc, int x1, int y1, int x2, int y2, uint8 color)
{
    x1 += grDx; y1 += grDy;
    x2 += grDx; y2 += grDy;

    platformLockSurface(sfc);

    // Bresenham's line drawing algorithm
    // Note : the code below intends to be pixel-perfect

    int dx, dy, cumul, x, y;
    int xinc, yinc;

    x = x1;
    y = y1;
    dx = abs(x2 - x1);
    dy = abs(y2 - y1);

    xinc = (x2>x1 ? 1 : -1);
    yinc = (y2>y1 ? 1 : -1);

    if (dy < dx) {
        cumul = (dx + 1) >> 1;

        for (int i=0; i <= dx; i++) {

            grPutPixel(sfc, x, y, color);

            x += xinc;
            cumul += dy;

            if (cumul > dx) {
                cumul -= dx;
                y += yinc;
            }
        }
    }
    else {
        cumul = (dy + 1) >> 1;

        for (int i=0; i <= dy; i++) {

            grPutPixel(sfc, x, y, color);

            y += yinc;
            cumul += dx;

            if (cumul > dy) {
                cumul -= dy;
                x += xinc;
            }
        }
    }

    platformUnlockSurface(sfc);
}


void grDrawRect(PlatformSurface *sfc, int x, int y, int width, int height, uint8 color)
{
    if (color >= 16) color = 0;
    x += grDx; y += grDy;
    x *= grScale; y *= grScale;
    width *= grScale; height *= grScale;

    PlatformRect dest = { x, y, width, height };
    platformFillRect(sfc, &dest,
                     ttmPalette[color][2],  // TODO ?
                     ttmPalette[color][1],
                     ttmPalette[color][0],
                     255
    );
}


void grDrawCircle(PlatformSurface *sfc, int x1, int y1, int width, int height, uint8 fgColor, uint8 bgColor)
{
    x1 += grDx; y1 += grDy;

    // We can only draw regular circles
    if (width != height) {
        fprintf(stderr, "Warning : grDrawCircle() : unable to draw ellipse\n");
        return;
    }

    // In original data, every width is even
    if (width % 2) {
        fprintf(stderr, "Warning : grDrawCircle() : unable to process odd diameters\n");
        return;
    }

    // Bresenham's circle drawing algorithm
    // Note : the code below intends to be pixel-perfect

    platformLockSurface(sfc);

    int r = (width >> 1) - 1;
    int xc = x1 + r;
    int yc = y1 + r;
    int x = 0;
    int y = r;
    int d = 1 - r;

    while (1) {

        grDrawHorizontalLine(sfc, xc-x, xc+x+1, yc+y+1, bgColor);
        grDrawHorizontalLine(sfc, xc-x, xc+x+1, yc-y  , bgColor);

        grDrawHorizontalLine(sfc, xc-y, xc+y+1, yc+x+1, bgColor);
        grDrawHorizontalLine(sfc, xc-y, xc+y+1, yc-x  , bgColor);

        if (y-x <= 1)
            break;

        if (d < 0)
            d += (x << 1) + 3;
        else {
            d += 2 * (x - y) + 5;
            y--;
        }

        x++;
    }

    if (fgColor != bgColor) {

        x = 0;
        y = r;
        d = 1 - r;

        while (1) {

            grPutPixel(sfc, xc-x  , yc+y+1, fgColor);
            grPutPixel(sfc, xc+x+1, yc+y+1, fgColor);

            grPutPixel(sfc, xc-x  , yc-y  , fgColor);
            grPutPixel(sfc, xc+x+1, yc-y  , fgColor);

            grPutPixel(sfc, xc-y  , yc+x+1, fgColor);
            grPutPixel(sfc, xc+y+1, yc+x+1, fgColor);

            grPutPixel(sfc, xc-y  , yc-x  , fgColor);
            grPutPixel(sfc, xc+y+1, yc-x  , fgColor);

            if (y-x <= 1)
                break;

            if (d < 0)
                d += (x << 1) + 3;
            else {
                d += 2 * (x - y) + 5;
                y--;
            }

            x++;
        }
    }

    platformUnlockSurface(sfc);
}


void grDrawSprite(PlatformSurface *sfc, struct TTtmSlot *ttmSlot, int x, int y, uint16 spriteNo, uint16 imageNo)
{
    if (imageNo >= MAX_BMP_SLOTS || spriteNo >= ttmSlot->numSprites[imageNo]) {
        debugMsg("Warning : grDrawSprite(): sprite #%d requested from bmpSlot %d (%s), but only %d loaded",
                spriteNo, imageNo,
                (imageNo < MAX_BMP_SLOTS && ttmSlot->bmpNames[imageNo]) ? ttmSlot->bmpNames[imageNo] : "empty",
                imageNo < MAX_BMP_SLOTS ? ttmSlot->numSprites[imageNo] : 0);
        return;
    }

    x += grDx; y += grDy;
    x *= grScale; y *= grScale;

    PlatformSurface *srcSfc = ttmSlot->sprites[imageNo][spriteNo];

    int assetDx, assetDy;
    artStyleSpriteOffset(ttmSlot->bmpNames[imageNo], spriteNo,
                        platformGetSurfaceWidth(srcSfc), platformGetSurfaceHeight(srcSfc),
                        0, &assetDx, &assetDy);
    x += assetDx; y += assetDy;
    PlatformRect dest = { x, y, 0, 0 };
    platformBlitSurface(srcSfc, NULL, sfc, &dest);
}


void grDrawSpriteAtop(PlatformSurface *sfc, struct TTtmSlot *ttmSlot, int x, int y, uint16 spriteNo, uint16 imageNo)
{
    if (imageNo >= MAX_BMP_SLOTS || spriteNo >= ttmSlot->numSprites[imageNo]) {
        debugMsg("Warning : grDrawSpriteAtop(): invalid sprite %u in slot %u", spriteNo, imageNo);
        return;
    }
    PlatformSurface *source = ttmSlot->sprites[imageNo][spriteNo];
    PlatformRect clip;
    platformGetClipRect(sfc, &clip);
    int dx = (x + grDx) * grScale, dy = (y + grDy) * grScale;
    int assetDx, assetDy;
    artStyleSpriteOffset(ttmSlot->bmpNames[imageNo], spriteNo,
                        platformGetSurfaceWidth(source), platformGetSurfaceHeight(source),
                        0, &assetDx, &assetDy);
    dx += assetDx; dy += assetDy;
    int left = dx > clip.x ? dx : clip.x;
    int top = dy > clip.y ? dy : clip.y;
    int right = dx + platformGetSurfaceWidth(source);
    int bottom = dy + platformGetSurfaceHeight(source);
    if (right > clip.x + clip.w) right = clip.x + clip.w;
    if (bottom > clip.y + clip.h) bottom = clip.y + clip.h;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > platformGetSurfaceWidth(sfc)) right = platformGetSurfaceWidth(sfc);
    if (bottom > platformGetSurfaceHeight(sfc)) bottom = platformGetSurfaceHeight(sfc);
    if (right <= left || bottom <= top) return;

    platformLockSurface(source);
    platformLockSurface(sfc);
    const uint8 *src = platformGetSurfacePixels(source);
    uint8 *dst = platformGetSurfacePixels(sfc);
    int sourcePitch = platformGetSurfacePitch(source);
    int destPitch = platformGetSurfacePitch(sfc);
    for (int py = top; py < bottom; py++) {
        for (int px = left; px < right; px++) {
            const uint8 *s = src + (size_t)(py - dy) * sourcePitch + (size_t)(px - dx) * 4;
            uint8 *d = dst + (size_t)py * destPitch + (size_t)px * 4;
            /* Retain Johnny's coverage; add the palm color as well as masking
             * Johnny's color. Over the already painted palm this equals palm
             * over Johnny over clean background, without drawing the palm twice.
             * Background/cloud/saved-zone pixels outside Johnny stay untouched. */
            for (int channel = 0; channel < 3; channel++)
                d[channel] = (uint8)((s[channel] * d[3] + d[channel] * (255 - s[3]) + 127) / 255);
        }
    }
    platformUnlockSurface(sfc);
    platformUnlockSurface(source);
}


void grDrawSpriteFlip(PlatformSurface *sfc, struct TTtmSlot *ttmSlot, int x, int y, uint16 spriteNo, uint16 imageNo)
{
    if (imageNo >= MAX_BMP_SLOTS || spriteNo >= ttmSlot->numSprites[imageNo]) {
        debugMsg("Warning : grDrawSpriteFlip(): sprite #%d requested from bmpSlot %d (%s), but only %d loaded",
                spriteNo, imageNo,
                (imageNo < MAX_BMP_SLOTS && ttmSlot->bmpNames[imageNo]) ? ttmSlot->bmpNames[imageNo] : "empty",
                imageNo < MAX_BMP_SLOTS ? ttmSlot->numSprites[imageNo] : 0);
        return;
    }

    x += grDx; y += grDy;
    x *= grScale; y *= grScale;

    PlatformSurface *srcSfc = ttmSlot->sprites[imageNo][spriteNo];
    int assetDx, assetDy;
    artStyleSpriteOffset(ttmSlot->bmpNames[imageNo], spriteNo,
                        platformGetSurfaceWidth(srcSfc), platformGetSurfaceHeight(srcSfc),
                        1, &assetDx, &assetDy);
    x += assetDx; y += assetDy;
    x += platformGetSurfaceWidth(srcSfc) - 1;

    for (int i=0; i < platformGetSurfaceWidth(srcSfc); i++) {

        PlatformRect src = { i, 0, 1, platformGetSurfaceHeight(srcSfc) };
        PlatformRect dest = { x - i, y, 0, 0 };

        platformBlitSurface(srcSfc, &src, sfc, &dest);
    }
}


void grClearScreen(PlatformSurface *sfc)
{
    PlatformRect rect;

    platformGetClipRect(sfc, &rect);
    platformSetClipRect(sfc, NULL);
    platformFillRect(sfc, NULL, 0, 0, 0, 0);
    platformSetClipRect(sfc, &rect);
}


void grLoadScreen(const char *strArg)
{
    struct TScrResource *scrResource = findScrResource(strArg);

    if (scrResource->width > SCREEN_WIDTH || scrResource->height > SCREEN_HEIGHT)
        fatalError("Screen resource is too big");

    if (scrResource->width % 2)
        fatalError("SCR '%s': unsupported odd width %u (packed pixels require an even width)",
                   scrResource->resName, scrResource->width);

    if (grBackgroundSfc != NULL)
        grReleaseScreen();

    if (grSavedZonesLayer != NULL)
        grReleaseSavedLayer();

    // The style loader selects a replacement or requests legacy decoding.
    if (grHdEnabled) {
        PlatformSurface *pngSfc = artStyleLoadScreen(scrResource->resName,
                                                    scrResource->width,
                                                    scrResource->height);
        if (pngSfc != NULL) {
            grBackgroundSfc = pngSfc;
            return;
        }
    }

    int w = scrResource->width;
    int h = scrResource->height;
    int outW = w * grScale;
    int outH = h * grScale;

    /*  Same unbounded pixel walk as grLoadBmp had: the loop below is sized from
     *  the file's own width/height and never consulted how many bytes actually
     *  decoded. All 10 shipped SCRs decode to exactly (width/2)*height bytes, so
     *  again there is no slack. */
    {
        size_t needed = ((size_t)w / 2) * (size_t)h;

        if (needed > (size_t)scrResource->uncompressedSize)
            fatalError("SCR '%s': %dx%d needs %zu pixel bytes, but only %u bytes were decoded",
                       scrResource->resName, w, h, needed, scrResource->uncompressedSize);
    }

    uint8 *outData = safe_malloc((size_t)outW * (size_t)outH * sizeof(uint32));
    uint8 *inPtr = scrResource->uncompressedData;

    int srcIndex = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {

            uint8 byte = inPtr[srcIndex];
            uint8 index = (x % 2 == 0) ? ((byte & 0xF0) >> 4) : (byte & 0x0F);
            if (x % 2 == 1)
                srcIndex++;

            int rx = x * grScale;
            int ry = y * grScale;

            for (int dy = 0; dy < grScale; dy++) {
                uint8 *row = outData + ((size_t)(ry + dy) * (size_t)outW + (size_t)rx) * 4;
                for (int dx = 0; dx < grScale; dx++) {
                    row[dx * 4 + 0] = ttmPalette[index][0];
                    row[dx * 4 + 1] = ttmPalette[index][1];
                    row[dx * 4 + 2] = ttmPalette[index][2];
                    row[dx * 4 + 3] = 255;
                }
            }
        }
    }

    grBackgroundSfc = platformCreateSurfaceFrom(outData, outW, outH, outW * 4);
    if (!grBackgroundSfc) {
        free(outData);
        fatalError("Could not create background surface: %s", platformGetError());
    }
}


// Some parts of the engine (ADS script runner) rely on having a valid background
// surface even when no SCR has been loaded yet.
//
// In the HD/scaled pipeline, the background must be allocated at the *render*
// resolution (logical 640x480 multiplied by grScale).
void grInitEmptyBackground(void)
{
    if (grBackgroundSfc != NULL)
        grReleaseScreen();

    if (grSavedZonesLayer != NULL)
        grReleaseSavedLayer();

    int outW = SCREEN_WIDTH * grScale;
    int outH = SCREEN_HEIGHT * grScale;

    uint8 *data = safe_malloc((size_t)outW * (size_t)outH * sizeof(uint32));

    // Opaque black background (alpha=255). This matters now that the blitter
    // supports real alpha blending.
    for (int y = 0; y < outH; y++) {
        uint8 *row = data + ((size_t)y * (size_t)outW * 4);
        for (int x = 0; x < outW; x++) {
            row[x * 4 + 0] = 0;   // B
            row[x * 4 + 1] = 0;   // G
            row[x * 4 + 2] = 0;   // R
            row[x * 4 + 3] = 255; // A
        }
    }

    grBackgroundSfc = platformCreateSurfaceFrom((void*)data, outW, outH, 4 * outW);
    if (!grBackgroundSfc) {
        free(data);
        fatalError("Could not create empty background: %s", platformGetError());
    }
}



// Release all sprite images for a given BMP slot.
// Note: sprite surfaces are created with platformCreateSurfaceFrom(), so the
// pixel buffers are owned by the engine and must be freed explicitly.
void grReleaseBmp(struct TTtmSlot *ttmSlot, uint16 bmpSlotNo)
{
    for (int i = 0; i < ttmSlot->numSprites[bmpSlotNo]; i++) {
        if (ttmSlot->sprites[bmpSlotNo][i] != NULL) {
            free(platformGetSurfacePixels(ttmSlot->sprites[bmpSlotNo][i]));
            platformFreeSurface(ttmSlot->sprites[bmpSlotNo][i]);
            ttmSlot->sprites[bmpSlotNo][i] = NULL;
        }
    }

    ttmSlot->numSprites[bmpSlotNo] = 0;
    if (ttmSlot->bmpNames[bmpSlotNo] != NULL) {
        free((void*)ttmSlot->bmpNames[bmpSlotNo]);
        ttmSlot->bmpNames[bmpSlotNo] = NULL;
    }
}


void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, const char *strArg)
{
    /* Fast path: slot already holds this exact BMP — skip the full
     * release-decode-reallocate cycle. Prevents a pathological per-frame
     * realloc storm if a TTM ever issues LOAD_IMAGE on an unchanged name
     * inside its animation loop. Normal scene authoring doesn't hit this,
     * but it's cheap defense. */
    if (ttmSlot->numSprites[slotNo] &&
        ttmSlot->bmpNames[slotNo] != NULL &&
        strcmp(ttmSlot->bmpNames[slotNo], strArg) == 0) {
        return;
    }

    {
        /* Copy before release: strArg may be the slot's own cached name.
         * A zero-image BMP still owns that name even though it has no sprites. */
        size_t len = strlen(strArg);
        char *nameCopy = safe_malloc(len + 1);
        memcpy(nameCopy, strArg, len + 1);
        grReleaseBmp(ttmSlot, slotNo);
        ttmSlot->bmpNames[slotNo] = nameCopy;
    }

    struct TBmpResource *bmpResource = findBmpResource(ttmSlot->bmpNames[slotNo]);
    /*  numImages is a uint16 straight out of the file and sprites[] holds
     *  MAX_SPRITES_PER_BMP pointers, so the loop below wrote past the slot as
     *  soon as a BMP declared more images than that - and numSprites was set
     *  from the same unchecked value, so every later read of the slot believed
     *  it. The shipped archive peaks at 102 images (LILIPUTS.BMP), so the cap
     *  has never been approached; nothing enforced it either. */
    if (bmpResource->numImages > MAX_SPRITES_PER_BMP)
        fatalError("BMP '%s': declares %u images, but a BMP slot holds at most %d",
                   bmpResource->resName, bmpResource->numImages, MAX_SPRITES_PER_BMP);

    uint8 *inPtr = bmpResource->uncompressedData;
    size_t srcUsed = 0;

    ttmSlot->numSprites[slotNo] = bmpResource->numImages;

    for (int image = 0; image < bmpResource->numImages; image++) {

        uint16 width  = bmpResource->widths[image];
        uint16 height = bmpResource->heights[image];

        if ((width % 2) == 1)
            fatalError("grLoadBmp(): can't manage odd widths");

        size_t spriteBytes = ((size_t)width * (size_t)height) / 2;

        /*  The pixel walk was bounded by the widths/heights table alone, which
         *  is also file-supplied, so a table claiming more pixels than were
         *  decoded read off the end of the heap buffer - once per pixel, for as
         *  many pixels as the file asked for. There is no slack to absorb it:
         *  all 116 shipped BMPs sum to EXACTLY their decoded size. */
        if (spriteBytes > (size_t)bmpResource->uncompressedSize - srcUsed)
            fatalError("BMP '%s': image %d (%ux%u) needs %zu pixel bytes at offset %zu, "
                       "but only %u bytes were decoded",
                       bmpResource->resName, image, width, height,
                       spriteBytes, srcUsed, bmpResource->uncompressedSize);

        srcUsed += spriteBytes;

        // Keep frame identity/geometry; the style loader owns alpha conventions.
        if (grHdEnabled) {
            PlatformSurface *pngSfc = artStyleLoadSprite(bmpResource->resName,
                                                        image, width, height);
            if (pngSfc != NULL) {
                ttmSlot->sprites[slotNo][image] = pngSfc;

                // still advance the source pointer for subsequent images
                inPtr += spriteBytes;
                continue;
            }
        }

        int outW = width * grScale;
        int outH = height * grScale;

        uint8 *outData = safe_malloc((size_t)outW * (size_t)outH * sizeof(uint32));

        // Decode nibble-packed pixels and scale-up by duplicating pixels.
        int srcIndex = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {

                uint8 byte = inPtr[srcIndex];
                uint8 index = (x % 2 == 0) ? ((byte & 0xF0) >> 4) : (byte & 0x0F);
                if (x % 2 == 1)
                    srcIndex++;

                int rx = x * grScale;
                int ry = y * grScale;

                for (int dy = 0; dy < grScale; dy++) {
                    uint8 *row = outData + ((size_t)(ry + dy) * (size_t)outW + (size_t)rx) * 4;
                    for (int dx = 0; dx < grScale; dx++) {
                        uint8 b = ttmPalette[index][0];
                        uint8 g = ttmPalette[index][1];
                        uint8 r = ttmPalette[index][2];

                        // In the original assets, the "magenta" color is used as a transparency key.
                        // Convert it to real alpha (0) instead of relying on a color key.
                        uint8 a = 255;
                        if (b == 0xA8 && g == 0x00 && r == 0xA8) {
                            a = 0;
                            b = g = r = 0; // premultiplied alpha: RGB must be 0 when A is 0
                        }

                        row[dx * 4 + 0] = b;
                        row[dx * 4 + 1] = g;
                        row[dx * 4 + 2] = r;
                        row[dx * 4 + 3] = a;
                    }
                }
            }
        }

        inPtr += spriteBytes;

        PlatformSurface *surface = platformCreateSurfaceFrom((void*)outData, outW, outH, 4 * outW);
        if (!surface) {
            free(outData);
            fatalError("Could not create sprite surface: %s", platformGetError());
        }
        ttmSlot->sprites[slotNo][image] = surface;
    }
}



void grFadeOut(void)
{
    static int fadeOutType = 0;
    PlatformSurface *sfc = platformGetWindowSurface(platform_window);

    /*  ALLOCATED LAZILY. Only the circle fade (case 0) needs a scratch layer -
     *  the other four draw rectangles straight onto the window surface and never
     *  touch it. Allocating it up front cost a full render-surface layer on
     *  every scene transition, which at the default HD scale is 1280x960x4 =
     *  4.9 MB malloc'd, zeroed and freed for nothing, four times out of five.
     */
    PlatformSurface *tmpSfc = NULL;

    int centerX = grScreenOrigin.w / 2;
    int centerY = grScreenOrigin.h / 2;

    grDx = grDy = 0;

    switch (fadeOutType) {

        // Circle from center
        case 0:
            // Note: we use tmpSfc to be sure we have a 32bpp surface,
            // which is needed by grDrawCircle()
            tmpSfc = grNewLayer();
            for (int radius=20; radius <= 400; radius += 20) {
                grDrawCircle(tmpSfc, centerX / grScale - radius, centerY / grScale - radius,
                    radius << 1, radius << 1, 5, 5);
                platformBlitSurface(tmpSfc, NULL, sfc, &grScreenOrigin);
                eventsWaitTick(1);
                platformUpdateWindow(platform_window);
            }
            break;

        // Rectangle from center
        case 1:
            for (int i=1; i <= 20; i++) {
                grDrawRect(sfc, grScreenOrigin.x + centerX / grScale - i*16, grScreenOrigin.y + centerY / grScale - i*12, i*32, i*24, 5);
                eventsWaitTick(1);
                platformUpdateWindow(platform_window);
            }
            break;

        // Right to left
        case 2:
            for (int i = SCREEN_WIDTH - 40; i >= 0; i -= 40) {
                grDrawRect(sfc, grScreenOrigin.x / grScale + i, grScreenOrigin.y / grScale, 40, SCREEN_HEIGHT, 5);
                eventsWaitTick(1);
                platformUpdateWindow(platform_window);
            }
            break;

        // Left to right
        case 3:
            for (int i=0; i < SCREEN_WIDTH; i += 40) {
                grDrawRect(sfc, grScreenOrigin.x + i, grScreenOrigin.y, 40, SCREEN_HEIGHT, 5);
                eventsWaitTick(1);
                platformUpdateWindow(platform_window);
            }
            break;

        // Middle to left and right
        case 4:
            for (int i=0; i < 320; i += 20) {
                grDrawRect(sfc, grScreenOrigin.x + centerX / grScale + i, grScreenOrigin.y, 20, SCREEN_HEIGHT, 5);
                grDrawRect(sfc, grScreenOrigin.x + centerX / grScale - 20 - i, grScreenOrigin.y, 20, SCREEN_HEIGHT, 5);
                eventsWaitTick(1);
                platformUpdateWindow(platform_window);
            }
            break;
    }

    grFreeLayer(tmpSfc);

    fadeOutType = (fadeOutType + 1) % 5;
}
