// Minimal PNG loader for HD override assets.
// On Windows: uses Windows Imaging Component (WIC) to decode PNG into 32bpp BGRA.
// On other platforms (macOS, Linux, Web): uses the minimal PNG decoder in
// png_decoder.c, which handles 8-bit RGB / RGBA / grayscale+alpha non-interlaced
// and falls back to NULL for anything more exotic.

#include "platform.h"
#include "png_decoder.h"
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>

#ifdef PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>

static int wic_initialized = 0;

PlatformSurface *platformLoadPNGFromMemory(const uint8 *data, size_t dataSize)
{
    if (!data || dataSize == 0)
        return NULL;

    if (!wic_initialized) {
        HRESULT hrInit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
            return NULL;
        wic_initialized = 1;
    }

    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    PlatformSurface *sfc = NULL;

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (LPVOID*)&factory);
    if (FAILED(hr) || !factory)
        goto mem_fail;

    hr = factory->lpVtbl->CreateStream(factory, &stream);
    if (FAILED(hr) || !stream)
        goto mem_fail;

    hr = stream->lpVtbl->InitializeFromMemory(stream, (BYTE*)data, (DWORD)dataSize);
    if (FAILED(hr))
        goto mem_fail;

    hr = factory->lpVtbl->CreateDecoderFromStream(factory, (IStream*)stream, NULL,
                                                   WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder)
        goto mem_fail;

    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    if (FAILED(hr) || !frame)
        goto mem_fail;

    hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    if (FAILED(hr) || !converter)
        goto mem_fail;

    hr = converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)frame,
                                       &GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, NULL, 0.0,
                                       WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        goto mem_fail;

    {
        UINT w = 0, h = 0;
        hr = converter->lpVtbl->GetSize(converter, &w, &h);
        if (FAILED(hr) || w == 0 || h == 0)
            goto mem_fail;

        if (w > UINT_MAX / 4) goto mem_fail;
        UINT stride = w * 4;
        if (h > 0 && stride > UINT_MAX / h) goto mem_fail;
        UINT bufferSize = stride * h;

        uint8 *pixels = (uint8*)malloc(bufferSize);
        if (!pixels)
            goto mem_fail;

        hr = converter->lpVtbl->CopyPixels(converter, NULL, stride, bufferSize, pixels);
        if (FAILED(hr)) {
            free(pixels);
            goto mem_fail;
        }

        sfc = platformCreateSurfaceFrom(pixels, (int)w, (int)h, (int)stride);
        if (!sfc) free(pixels);
    }
    goto mem_cleanup;

mem_fail:
    sfc = NULL;

mem_cleanup:
    if (converter) converter->lpVtbl->Release(converter);
    if (frame) frame->lpVtbl->Release(frame);
    if (decoder) decoder->lpVtbl->Release(decoder);
    if (stream) stream->lpVtbl->Release(stream);
    if (factory) factory->lpVtbl->Release(factory);
    return sfc;
}

#else  // !PLATFORM_WINDOWS

/*
 * macOS, Linux, and Web: decode via the minimal PNG decoder in png_decoder.c.
 * The decoder returns a malloc'd 32bpp BGRA buffer that the engine takes
 * ownership of via platformCreateSurfaceFrom() — the buffer is freed by
 * grReleaseBmp() / grReleaseScreen() when the sprite/screen is released,
 * matching the lifetime model used by the Windows WIC path above.
 *
 * pngDecodeToBGRA() returns NULL for unsupported formats (16-bit depth,
 * paletted, Adam7-interlaced). Returning NULL here causes the caller
 * (grLoadBmp / grLoadScreen) to fall back to the legacy RESOURCE.001
 * decoder for that asset — so HD mode degrades gracefully per asset.
 */
PlatformSurface *platformLoadPNGFromMemory(const uint8 *data, size_t dataSize)
{
    int w = 0, h = 0;
    uint8 *pixels = pngDecodeToBGRA(data, dataSize, &w, &h);
    if (!pixels)
        return NULL;

    PlatformSurface *sfc = platformCreateSurfaceFrom(pixels, w, h, w * 4);
    if (!sfc) {
        free(pixels);
        return NULL;
    }
    return sfc;
}

#endif
