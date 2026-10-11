/*
 *  Minimal PNG decoder for Johnny Reborn HD.
 *
 *  Handles the common cases exported by modern art tools:
 *    - 8-bit RGB (PNG color type 2)
 *    - 8-bit RGBA (PNG color type 6)
 *    - 8-bit grayscale + alpha (PNG color type 4)
 *    - Non-interlaced
 *    - All 5 filter types (None, Sub, Up, Average, Paeth)
 *
 *  Unsupported formats (16-bit depth, paletted, Adam7 interlaced) return NULL;
 *  the engine's existing fallback path — decode the legacy RESOURCE.001 asset
 *  and nearest-neighbor-scale it — will take over for that one asset.
 *
 *  Inflation uses the already-vendored miniz (`tinfl`), so this adds no new
 *  third-party dependencies.
 */

#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include <stddef.h>
#include "mytypes.h"

/*
 * Decode a PNG buffer into a 32bpp BGRA pixel buffer (alpha in byte 3).
 *
 * On success:
 *   - Returns malloc'd buffer of size (*outWidth * *outHeight * 4) bytes.
 *   - *outWidth and *outHeight are filled.
 *   - Caller must free() the returned buffer.
 *
 * On failure (malformed input, unsupported format, OOM):
 *   - Returns NULL. *outWidth and *outHeight are not modified.
 */
uint8 *pngDecodeToBGRA(const uint8 *data, size_t dataSize,
                       int *outWidth, int *outHeight);

#endif /* PNG_DECODER_H */
