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
#include "utils.h"

static int nextbit;
static uint8 current;
static uint32 inOffset;
static uint32 maxInOffset;

struct TCodeTableEntry {
    uint16 prefix;
    uint8 append;
};


static uint8 getByte(FILE *f)
{
    if (inOffset >= maxInOffset) {
        return 0;
    }
    else {
        inOffset++;
        int c = fgetc(f);
        if (c == EOF)
            return 0;
        return (uint8)c;
    }
}


static uint16 getBits(FILE *f, uint8 n)
{
    if (n == 0)
        return 0;

    uint16 x = 0;

    for (uint8 i=0; i < n; i++) {
        if (current & (uint8)(1u << nextbit))
            x |= (uint16)(1u << i);

        nextbit++;

        if (nextbit > 7) {
            current = (uint8) getByte(f);
            nextbit = 0;
        }
    }

    return x;
}


static void skipBits(FILE *f, uint32 n)
{
    for (uint32 i=0; i < n; i++) {

        nextbit++;

        if (nextbit > 7) {
            current = getByte(f);
            nextbit = 0;
        }
    }
}


/*
 * LZW decoder. 9..12-bit variable-width codes; code 256 resets the dictionary.
 *
 * Bounds invariants (enforced inline; see corresponding breaks/checks):
 *   - decodeStack[] is 4096 entries; stackPtr < 4096 at every write.
 *   - codeTable[] is 4096 entries; reads gated by `code > 4095` break,
 *     writes gated by `free_entry < 4096` check.
 *   - outData[] is outSize bytes; writes gated by `outOffset >= outSize`
 *     (returns early, which is correct: "image fully decoded, stop reading input").
 *   - inOffset never exceeds maxInOffset because getByte() caps at maxInOffset
 *     (returns 0 past EOF rather than advancing).
 *
 * Corrupt input produces (potentially garbage) output, never a crash. If the
 * input doesn't consume exactly inSize bytes we emit a fatalError with offset
 * context so the failure is diagnosable.
 */
uint8 *uncompressLZW(FILE *f, uint32 inSize, uint32 outSize)
{
    uint8  *outData;
    struct TCodeTableEntry codeTable[4096];
    memset(codeTable, 0, sizeof(codeTable));
    uint8  decodeStack[4096];
    uint32 stackPtr = 0;
    uint8  n_bits = 9;
    uint32 free_entry = 257;
    uint16 oldcode;
    uint16 lastbyte;
    uint32 bitpos = 0;
    uint32 outOffset = 0;
    const char *earlyBreakReason = NULL;


    if (outSize == 0)
        fatalError("uncompressLZW() : can't uncompress to 0 bytes\n");

    maxInOffset = inSize;
    nextbit     = 0;
    inOffset    = 0;
    outData     = safe_malloc(outSize * sizeof(uint8));

    current  = getByte(f);
    lastbyte = oldcode = getBits(f, n_bits);

    outData[outOffset++] = (uint8) oldcode;

    while (inOffset < inSize) {

        uint16 newcode = getBits(f, n_bits);
        bitpos += n_bits;

        if (newcode == 256) {

            uint32 nbits3 = ((uint32)n_bits) << 3;
            uint32 nskip = (nbits3 - ((bitpos - 1) % nbits3)) - 1;
            skipBits(f, nskip);
            n_bits = 9;
            free_entry = 256;
            bitpos = 0;
        }
        else {

            uint16 code = newcode;

            if (code >= free_entry) {

                if (stackPtr >= 4096) {
                    earlyBreakReason = "decodeStack overflow at (code >= free_entry) handler";
                    break;
                }

                decodeStack[stackPtr] = (uint8) lastbyte;
                stackPtr++;
                code = oldcode;
            }

            while (code > 255) {

                /* codeTable index bounds (code <= 4095) + decodeStack bounds (stackPtr < 4096).
                 * A corrupt codeTable cycle is bounded by the stackPtr limit. */
                if (code > 4095 || stackPtr >= 4096) {
                    earlyBreakReason = (code > 4095)
                        ? "codeTable index out of range"
                        : "decodeStack overflow in prefix chain walk";
                    break;
                }

                decodeStack[stackPtr] = codeTable[code].append;
                stackPtr++;
                code = codeTable[code].prefix;
            }

            if (stackPtr >= 4096) {
                earlyBreakReason = "decodeStack overflow before final push";
                break;
            }

            decodeStack[stackPtr] = (uint8) code;
            stackPtr++;
            lastbyte = code;

            while (stackPtr > 0) {

                stackPtr--;

                /* Output-buffer full: successful early termination. Image is
                 * fully decoded. Remaining stack entries are discarded. */
                if (outOffset >= outSize)
                    return outData;

                outData[outOffset++] = decodeStack[stackPtr];
            }

            if (free_entry < 4096) {

                codeTable[free_entry].prefix = (uint16)oldcode;
                codeTable[free_entry].append = (uint8)lastbyte;
                free_entry++;
                uint32 temp = 1 << n_bits;

                if (free_entry >= temp && n_bits < 12) {
                    n_bits++;
                    bitpos = 0;
                }
            }

            oldcode = newcode;
        }
    }

    if (earlyBreakReason != NULL) {
        fatalError("LZW decode aborted: %s (inOffset=%u/%u, outOffset=%u/%u, "
                   "stackPtr=%u, free_entry=%u, n_bits=%u)",
                   earlyBreakReason, (unsigned)inOffset, (unsigned)inSize,
                   (unsigned)outOffset, (unsigned)outSize,
                   (unsigned)stackPtr, (unsigned)free_entry, (unsigned)n_bits);
    }

    if (outOffset != outSize) {
        fatalError("LZW decode incomplete: produced %u of %u output bytes "
                   "(consumed %u of %u input bytes)",
                   (unsigned)outOffset, (unsigned)outSize,
                   (unsigned)inOffset, (unsigned)inSize);
    }

    if (inOffset != inSize) {
        fatalError("LZW decode truncated: consumed %u of %u input bytes "
                   "(produced %u of %u output bytes)",
                   (unsigned)inOffset, (unsigned)inSize,
                   (unsigned)outOffset, (unsigned)outSize);
    }

    return outData;
}


uint8 *uncompressRLE(FILE *f, uint32 inSize, uint32 outSize)
{
    uint8 *outData;
    uint32 outOffset = 0;

    inOffset    = 0;
    maxInOffset = inSize;

    outData = safe_malloc(outSize * sizeof(uint8));

    while (outOffset < outSize && inOffset < maxInOffset) {

        int c = fgetc(f);
        if (c == EOF) break;
        uint8 control = (uint8)c;
        inOffset++;

        if ((control & 0x80) == 0x80) {
            uint8 length = control & 0x7F;
            c = fgetc(f);
            if (c == EOF) break;
            uint8 b = (uint8)c;
            inOffset++;

            for (int i=0; i < length && outOffset < outSize; i++)
                outData[outOffset++] = b;
        }
        else {
            for (int i=0;  i < control && outOffset < outSize; i++) {
                c = fgetc(f);
                if (c == EOF) break;
                outData[outOffset++] = (uint8)c;
                inOffset++;
            }
        }
    }

    if (outOffset != outSize) {
        fatalError("RLE decode incomplete: produced %u of %u output bytes "
                   "(consumed %u of %u input bytes)",
                   (unsigned)outOffset, (unsigned)outSize,
                   (unsigned)inOffset, (unsigned)inSize);
    }

    if (inOffset != inSize) {
        fatalError("RLE decode truncated: consumed %u of %u input bytes "
                   "(produced %u of %u output bytes)",
                   (unsigned)inOffset, (unsigned)inSize,
                   (unsigned)outOffset, (unsigned)outSize);
    }

    return outData;
}


uint8 *uncompress(FILE *f, uint8 compressionMethod, uint32 inSize, uint32 outSize)
{
    switch (compressionMethod) {

        case 1:
            return uncompressRLE(f, inSize, outSize);

        case 2:
            return uncompressLZW(f, inSize, outSize);

        default:
            /* fatalError is [[noreturn]]; no fallback return needed. */
            fatalError("Unknown compression method: %d", compressionMethod);
    }
}

