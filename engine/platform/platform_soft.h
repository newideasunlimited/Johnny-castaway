/*
 *  Software surface implementation shared by the backends that have no native
 *  surface type of their own: the headless test backend and the macOS screen
 *  saver backend. Each of the upstream backends carries its own copy of this
 *  code; here it lives once.
 *
 *  Pixel format is 32-bit premultiplied BGRA (B=0, G=1, R=2, A=3), which is
 *  what the engine's palette loader and PNG decoder produce.
 *
 *  This file is part of 'Johnny Reborn' (GPL-3.0-or-later).
 */

#ifndef PLATFORM_SOFT_H
#define PLATFORM_SOFT_H

#include "platform.h"

struct PlatformSurface {
    int width;
    int height;
    int pitch;
    int bytesPerPixel;
    uint8* pixels;
    PlatformRect clipRect;
    int ownPixels;   /* 1 if we allocated pixels, 0 if borrowed */
};

/* Backends that include this must define their own lastError storage and
 * expose it through this setter so the surface code can report failures. */
void platformSoftSetError(const char* message);

#endif /* PLATFORM_SOFT_H */
