/*
 *  Minimal PNG decoder for Johnny Reborn HD.
 *  See png_decoder.h for supported formats and return contract.
 */

#include "png_decoder.h"

#include <stdlib.h>
#include <string.h>

#include "miniz.h"   /* tinfl_decompress_mem_to_heap, TINFL_FLAG_PARSE_ZLIB_HEADER */

/* ---- Helpers ------------------------------------------------------------- */

static uint32 read_u32be(const uint8 *p)
{
    return ((uint32)p[0] << 24) |
           ((uint32)p[1] << 16) |
           ((uint32)p[2] <<  8) |
            (uint32)p[3];
}

/* PaethPredictor per PNG 11.2.2: returns whichever of a, b, c is closest
 * to (a + b - c). Ties broken by the order a, b, c. */
static int paethPredictor(int a, int b, int c)
{
    int p  = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

/* ---- Decoder ------------------------------------------------------------- */

uint8 *pngDecodeToBGRA(const uint8 *data, size_t dataSize,
                       int *outWidth, int *outHeight)
{
    /* Minimum valid PNG = signature(8) + IHDR(25) + IEND(12) = 45 bytes. */
    static const uint8 SIG[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (!data || dataSize < 45 || memcmp(data, SIG, 8) != 0)
        return NULL;

    size_t pos = 8;

    int width = 0, height = 0;
    int bitDepth = 0, colorType = 0, interlace = 0;
    int haveIhdr = 0;

    /* Concatenated IDAT buffer (PNG spec allows multiple IDAT chunks). */
    uint8 *idatBuf = NULL;
    size_t idatCap = 0;
    size_t idatSize = 0;

    uint8 *raw = NULL;     /* inflated raw scanlines (filter-byte + pixels) */
    uint8 *decoded = NULL; /* unfiltered scanlines */
    uint8 *out = NULL;     /* BGRA output */

    /* ---- Walk chunks --------------------------------------------------- */
    while (pos + 12 <= dataSize) {
        uint32 chunkLen  = read_u32be(data + pos);
        uint32 chunkType = read_u32be(data + pos + 4);
        const uint8 *chunkData = data + pos + 8;

        /* Bounds: chunk header (8) + data + CRC (4) must fit. */
        if (chunkLen > dataSize || pos + 8 + chunkLen + 4 > dataSize)
            goto fail;

        if (chunkType == 0x49484452u) {  /* "IHDR" */
            if (chunkLen != 13) goto fail;
            width     = (int)read_u32be(chunkData + 0);
            height    = (int)read_u32be(chunkData + 4);
            bitDepth  = chunkData[8];
            colorType = chunkData[9];
            /* chunkData[10] compression method — must be 0 */
            /* chunkData[11] filter method      — must be 0 */
            interlace = chunkData[12];
            haveIhdr  = 1;

            /* Scope limits: only the common 8-bit non-interlaced formats. */
            if (bitDepth != 8)                                goto fail;
            if (colorType != 2 && colorType != 4 && colorType != 6) goto fail;
            if (interlace != 0)                               goto fail;
            if (chunkData[10] != 0 || chunkData[11] != 0)     goto fail;
            if (width <= 0 || height <= 0)                    goto fail;
            if (width > 16384 || height > 16384)              goto fail;  /* sanity */
        }
        else if (chunkType == 0x49444154u) {  /* "IDAT" */
            if (!haveIhdr) goto fail;
            size_t needed = idatSize + chunkLen;
            if (needed < idatSize) goto fail;  /* overflow guard */

            if (needed > idatCap) {
                size_t newCap = idatCap ? idatCap * 2 : 8192;
                while (newCap < needed) {
                    size_t doubled = newCap * 2;
                    if (doubled < newCap) goto fail;  /* overflow */
                    newCap = doubled;
                }
                uint8 *resized = (uint8 *)realloc(idatBuf, newCap);
                if (!resized) goto fail;
                idatBuf = resized;
                idatCap = newCap;
            }
            memcpy(idatBuf + idatSize, chunkData, chunkLen);
            idatSize += chunkLen;
        }
        else if (chunkType == 0x49454E44u) {  /* "IEND" */
            break;
        }
        /* Unknown or unsupported chunks (PLTE, tRNS, gAMA, ...) are skipped.
         * This is safe per PNG spec for chunks whose first byte bit 5 is set
         * (ancillary); we skip critical unknown chunks too for simplicity,
         * accepting that images using them may decode incorrectly. Those
         * images will typically be rejected at the IHDR stage (paletted). */

        pos += 8 + chunkLen + 4;  /* header + data + CRC */
    }

    if (!haveIhdr || idatSize == 0) goto fail;

    /* ---- Decode ------------------------------------------------------- */

    /* bytes per pixel in the *decoded* stream (before we convert to BGRA) */
    int bpp;
    switch (colorType) {
        case 2: bpp = 3; break;  /* RGB */
        case 4: bpp = 2; break;  /* Grayscale + Alpha */
        case 6: bpp = 4; break;  /* RGBA */
        default: goto fail;
    }

    size_t scanlineBytes = (size_t)width * (size_t)bpp;
    size_t rawExpected   = (scanlineBytes + 1) * (size_t)height;

    /* Inflate the concatenated IDAT zlib stream. */
    size_t inflatedSize = 0;
    raw = (uint8 *)tinfl_decompress_mem_to_heap(
        idatBuf, idatSize, &inflatedSize, TINFL_FLAG_PARSE_ZLIB_HEADER);
    free(idatBuf); idatBuf = NULL;

    if (!raw || inflatedSize != rawExpected) goto fail;

    /* Unfilter scanlines into a separate buffer (some filters reference the
     * previous decoded row or previous decoded bytes in the same row). */
    decoded = (uint8 *)malloc(scanlineBytes * (size_t)height);
    if (!decoded) goto fail;

    for (int y = 0; y < height; y++) {
        uint8       filterType = raw[(size_t)y * (scanlineBytes + 1)];
        const uint8 *src       = raw     + (size_t)y * (scanlineBytes + 1) + 1;
        uint8       *dst       = decoded + (size_t)y * scanlineBytes;
        const uint8 *prev      = (y > 0) ? (decoded + (size_t)(y - 1) * scanlineBytes)
                                         : NULL;

        switch (filterType) {
            case 0:  /* None */
                memcpy(dst, src, scanlineBytes);
                break;

            case 1:  /* Sub — left neighbor */
                for (size_t i = 0; i < scanlineBytes; i++) {
                    uint8 a = (i >= (size_t)bpp) ? dst[i - bpp] : 0;
                    dst[i] = (uint8)(src[i] + a);
                }
                break;

            case 2:  /* Up — previous row */
                for (size_t i = 0; i < scanlineBytes; i++) {
                    uint8 b = prev ? prev[i] : 0;
                    dst[i] = (uint8)(src[i] + b);
                }
                break;

            case 3:  /* Average */
                for (size_t i = 0; i < scanlineBytes; i++) {
                    uint8 a = (i >= (size_t)bpp) ? dst[i - bpp] : 0;
                    uint8 b = prev ? prev[i] : 0;
                    dst[i] = (uint8)(src[i] + (a + b) / 2);
                }
                break;

            case 4:  /* Paeth */
                for (size_t i = 0; i < scanlineBytes; i++) {
                    int a = (i >= (size_t)bpp)                       ? dst[i - bpp]       : 0;
                    int b = prev                                     ? prev[i]            : 0;
                    int c = (prev && i >= (size_t)bpp)               ? prev[i - bpp]      : 0;
                    dst[i] = (uint8)(src[i] + paethPredictor(a, b, c));
                }
                break;

            default:
                goto fail;
        }
    }

    free(raw); raw = NULL;

    /* Convert to BGRA (engine's expected surface format). */
    size_t outSize = (size_t)width * (size_t)height * 4u;
    out = (uint8 *)malloc(outSize);
    if (!out) goto fail;

    for (int y = 0; y < height; y++) {
        const uint8 *s = decoded + (size_t)y * scanlineBytes;
        uint8       *d = out     + (size_t)y * (size_t)width * 4u;

        for (int x = 0; x < width; x++) {
            uint8 r, g, b, a;
            switch (colorType) {
                case 2:  /* RGB */
                    r = s[x * 3 + 0]; g = s[x * 3 + 1]; b = s[x * 3 + 2]; a = 255;
                    break;
                case 4:  /* Grayscale + Alpha */
                    r = g = b = s[x * 2 + 0];           a = s[x * 2 + 1];
                    break;
                case 6:  /* RGBA */
                    r = s[x * 4 + 0]; g = s[x * 4 + 1]; b = s[x * 4 + 2]; a = s[x * 4 + 3];
                    break;
                default:
                    r = g = b = 0; a = 255;  /* unreachable; colorType was validated */
                    break;
            }
            /*  PREMULTIPLY, because that is what every consumer of this buffer
             *  already assumes.
             *
             *  All four platformBlitSurface implementations do a premultiplied
             *  source-over - they add the source colour raw rather than scaling
             *  it by alpha - and platformFillRect premultiplies its own
             *  arguments before storing, which is the codebase stating the
             *  surface format outright. On Windows the question never arose,
             *  because png_loader.c asks WIC for 32bppPBGRA and WIC does this
             *  multiply itself; this decoder is the non-Windows path and emitted
             *  straight alpha, so the two disagreed.
             *
             *  LATENT, not a live bug, and worth being precise about: straight
             *  and premultiplied are identical wherever alpha is 0 or 255, and
             *  measured across all 2,402 HD PNGs in the shipped archive -
             *  39,254,880 pixels - there is not one partially transparent pixel.
             *  Both blitter fast paths absorb every pixel today, so no halo can
             *  appear. It fires the moment anti-aliased art is added, which for
             *  an HD fork is a matter of time rather than an exotic scenario.
             */
            if (a == 0) {
                r = g = b = 0;
            }
            else if (a != 255) {
                r = (uint8)((r * a + 127) / 255);
                g = (uint8)((g * a + 127) / 255);
                b = (uint8)((b * a + 127) / 255);
            }

            d[x * 4 + 0] = b;
            d[x * 4 + 1] = g;
            d[x * 4 + 2] = r;
            d[x * 4 + 3] = a;
        }
    }

    free(decoded); decoded = NULL;

    if (outWidth)  *outWidth  = width;
    if (outHeight) *outHeight = height;
    return out;

fail:
    free(idatBuf);
    free(raw);
    free(decoded);
    free(out);
    return NULL;
}
