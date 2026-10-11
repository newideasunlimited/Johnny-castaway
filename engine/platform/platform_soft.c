/*
 *  Software surfaces: create/free, blit with premultiplied alpha, fill, clip.
 *  Shared by platform_headless.c and platform_saver.c. See platform_soft.h.
 *
 *  Derived from the surface code in the upstream macOS and Linux backends.
 *  This file is part of 'Johnny Reborn' (GPL-3.0-or-later).
 */

#include <stdlib.h>
#include <string.h>

#include "platform_soft.h"

PlatformSurface* platformCreateSurface(int width, int height) {
    if (width <= 0 || height <= 0) {
        platformSoftSetError("Invalid surface size");
        return NULL;
    }
    PlatformSurface* surface = (PlatformSurface*)malloc(sizeof(PlatformSurface));
    if (!surface) { platformSoftSetError("Out of memory allocating surface"); return NULL; }
    surface->width = width;
    surface->height = height;
    surface->bytesPerPixel = 4;
    surface->pitch = width * 4;
    surface->pixels = (uint8*)calloc((size_t)width * (size_t)height, 4);
    if (!surface->pixels) {
        free(surface);
        platformSoftSetError("Out of memory allocating surface pixels");
        return NULL;
    }
    surface->clipRect.x = 0;
    surface->clipRect.y = 0;
    surface->clipRect.w = width;
    surface->clipRect.h = height;
    surface->ownPixels = 1;
    return surface;
}

PlatformSurface* platformCreateSurfaceFrom(void* pixels, int width, int height, int pitch) {
    PlatformSurface* surface = (PlatformSurface*)malloc(sizeof(PlatformSurface));
    if (!surface) { platformSoftSetError("Out of memory allocating surface wrapper"); return NULL; }
    surface->width = width;
    surface->height = height;
    surface->bytesPerPixel = 4;
    surface->pitch = pitch;
    surface->pixels = (uint8*)pixels;
    surface->clipRect.x = 0;
    surface->clipRect.y = 0;
    surface->clipRect.w = width;
    surface->clipRect.h = height;
    surface->ownPixels = 0;
    return surface;
}

void platformFreeSurface(PlatformSurface* surface) {
    if (surface) {
        if (surface->ownPixels && surface->pixels) {
            free(surface->pixels);
        }
        free(surface);
    }
}

void platformLockSurface(PlatformSurface* surface) { UNUSED(surface); }
void platformUnlockSurface(PlatformSurface* surface) { UNUSED(surface); }

void platformBlitSurface(PlatformSurface* src, PlatformRect* srcRect,
                        PlatformSurface* dst, PlatformRect* dstRect) {
    if (!src || !dst || !src->pixels || !dst->pixels) return;

    int srcX = srcRect ? srcRect->x : 0;
    int srcY = srcRect ? srcRect->y : 0;
    int srcW = srcRect ? srcRect->w : src->width;
    int srcH = srcRect ? srcRect->h : src->height;

    int dstX = dstRect ? dstRect->x : 0;
    int dstY = dstRect ? dstRect->y : 0;

    /* Clip to the source bounds first; moving the source origin moves the
     * destination origin by the same amount. */
    if (srcX < 0) { srcW += srcX; dstX -= srcX; srcX = 0; }
    if (srcY < 0) { srcH += srcY; dstY -= srcY; srcY = 0; }
    if (srcX + srcW > src->width)  srcW = src->width  - srcX;
    if (srcY + srcH > src->height) srcH = src->height - srcY;

    /* Then to the destination clip rect, intersected with its bounds. */
    {
        int cx0 = dst->clipRect.x, cy0 = dst->clipRect.y;
        int cx1 = cx0 + dst->clipRect.w, cy1 = cy0 + dst->clipRect.h;
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (cx1 > dst->width)  cx1 = dst->width;
        if (cy1 > dst->height) cy1 = dst->height;

        if (dstX < cx0) { srcX += cx0 - dstX; srcW -= cx0 - dstX; dstX = cx0; }
        if (dstY < cy0) { srcY += cy0 - dstY; srcH -= cy0 - dstY; dstY = cy0; }
        if (dstX + srcW > cx1) srcW = cx1 - dstX;
        if (dstY + srcH > cy1) srcH = cy1 - dstY;
    }

    if (srcW <= 0 || srcH <= 0) return;

    for (int y = 0; y < srcH; y++) {
        const uint8* srcRow = src->pixels + (size_t)(srcY + y) * (size_t)src->pitch + (size_t)srcX * 4;
        uint8* dstRow = dst->pixels + (size_t)(dstY + y) * (size_t)dst->pitch + (size_t)dstX * 4;

        for (int x = 0; x < srcW; x++) {
            const uint8* srcPixel = srcRow + (size_t)x * 4;
            uint8* dstPixel = dstRow + (size_t)x * 4;
            uint8 sa = srcPixel[3];

            if (sa == 255) {
                memcpy(dstPixel, srcPixel, 4);
                continue;
            }
            if (sa == 0) {
                continue;
            }

            int inv = 255 - sa;
            /* premultiplied-alpha "source over" */
            dstPixel[0] = (uint8)(srcPixel[0] + (dstPixel[0] * inv + 127) / 255);
            dstPixel[1] = (uint8)(srcPixel[1] + (dstPixel[1] * inv + 127) / 255);
            dstPixel[2] = (uint8)(srcPixel[2] + (dstPixel[2] * inv + 127) / 255);
            dstPixel[3] = (uint8)(sa + (dstPixel[3] * inv + 127) / 255);
        }
    }
}

void platformFillRect(PlatformSurface* surface, PlatformRect* rect,
                     uint8 r, uint8 g, uint8 b, uint8 a) {
    if (!surface || !surface->pixels) return;

    int x = rect ? rect->x : 0;
    int y = rect ? rect->y : 0;
    int w = rect ? rect->w : surface->width;
    int h = rect ? rect->h : surface->height;

    /* Premultiply (BGRA / PBGRA). */
    uint8 pb = b, pg = g, pr = r;
    if (a == 0) {
        pb = pg = pr = 0;
    }
    else if (a != 255) {
        pb = (uint8)((b * a + 127) / 255);
        pg = (uint8)((g * a + 127) / 255);
        pr = (uint8)((r * a + 127) / 255);
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > surface->width)  w = surface->width - x;
    if (y + h > surface->height) h = surface->height - y;
    if (w <= 0 || h <= 0) return;

    for (int py = y; py < y + h; py++) {
        uint8* pixel = surface->pixels + (size_t)py * (size_t)surface->pitch + (size_t)x * 4;
        for (int px = 0; px < w; px++, pixel += 4) {
            pixel[0] = pb;
            pixel[1] = pg;
            pixel[2] = pr;
            pixel[3] = a;
        }
    }
}

void platformSetClipRect(PlatformSurface* surface, PlatformRect* rect) {
    if (!surface) return;
    if (rect) {
        surface->clipRect = *rect;
    } else {
        surface->clipRect.x = 0;
        surface->clipRect.y = 0;
        surface->clipRect.w = surface->width;
        surface->clipRect.h = surface->height;
    }
}

void platformGetClipRect(PlatformSurface* surface, PlatformRect* rect) {
    if (surface && rect) *rect = surface->clipRect;
}

uint8* platformGetSurfacePixels(PlatformSurface* surface) { return surface ? surface->pixels : NULL; }
int platformGetSurfacePitch(PlatformSurface* surface) { return surface ? surface->pitch : 0; }
int platformGetSurfaceWidth(PlatformSurface* surface) { return surface ? surface->width : 0; }
int platformGetSurfaceHeight(PlatformSurface* surface) { return surface ? surface->height : 0; }
int platformGetSurfaceBytesPerPixel(PlatformSurface* surface) { return surface ? surface->bytesPerPixel : 0; }
