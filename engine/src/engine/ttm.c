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

#include <stdio.h>

#include "mytypes.h"
#include "utils.h"
#include "resource.h"
#include "graphics.h"
#include "sound.h"
#include "ttm.h"


int ttmDx = 0;
int ttmDy = 0;


/*  SIZED FOR THE ENCODING, not for the widest opcode anyone has seen.
 *
 *  A TTM opcode's low nibble is its argument count: 0x0f selects the string
 *  form, so the numeric form admits 0..14 words. The buffer here held 10, and
 *  peekUint16Block wrote all of them - a data-driven overflow of up to 8 bytes
 *  of stack. The shipped archive only ever uses 0, 1, 2, 4 and 6 (measured over
 *  all 41 TTMs), which is why it never showed. dump.c's copy of the same decoder
 *  already used 20, so the range was known.
 */
#define TTM_MAX_ARGS 16


static uint32 ttmFindPreviousTag(struct TTtmSlot *ttmSlot, uint32 offset)
{
    uint32 result = 0;

    for (int i = 0; i < ttmSlot->numTags; i++) {
        if (ttmSlot->tags[i].offset >= offset)
            break;
        result = ttmSlot->tags[i].offset;
    }

    return result;
}


uint32 ttmFindTag(struct TTtmSlot *ttmSlot, uint16 reqdTag)
{
    uint32 result = 0;
    int i = 0;

    while (result == 0 && i < ttmSlot->numTags) {

        if (ttmSlot->tags[i].id == reqdTag)
            result = ttmSlot->tags[i].offset;
        else
            i++;
    }

    if (result == 0)
        fprintf(stderr, "Warning : TTM tag #%d not found, returning offset 0000\n", reqdTag);

    return result;
}


void ttmLoadTtm(struct TTtmSlot *ttmSlot, const char *ttmName)
{
    struct TTtmResource *ttmResource = findTtmResource(ttmName);
    debugMsg("---- Loading %s", ttmResource->resName);

    ttmSlot->data     = ttmResource->uncompressedData;
    ttmSlot->dataSize = ttmResource->uncompressedSize;
    ttmSlot->numTags  = ttmResource->numTags;
    ttmSlot->tags     = safe_malloc((size_t)ttmSlot->numTags * sizeof(struct TTtmTag));

    // we have to bookmark every tag for later jumps
    uint32 offset=0;
    int tagNo = 0;

    while (peekHasBytes(ttmSlot->dataSize, offset, 2)) {

        uint32 opcodeOffset = offset;
        uint16 opcode = peekUint16(ttmSlot->data, ttmSlot->dataSize, &offset,
                                   ttmResource->resName);

        if (opcode == 0x1111 || opcode == 0x1101) {

            /*  REFUSE rather than write past the tag table.
             *
             *  `tags` is allocated from the TAG: chunk's numTags but filled by
             *  scanning the bytecode, and nothing tied the two together. There
             *  is no margin to absorb a disagreement either: measured across all
             *  41 shipped TTMs the scan finds EXACTLY numTags tags in every one,
             *  so a single extra 0x1111/0x1101 opcode in a corrupt or crafted
             *  script overruns the heap block by a whole TTtmTag.
             *
             *  The sentinel fill below still handles the documented opposite
             *  case (fewer tags found than declared, see the SASKDATE.TTM TODO).
             */
            if (tagNo >= ttmSlot->numTags)
                fatalError("TTM %s: bytecode declares more tags than its TAG: chunk "
                           "(%d); tag opcode %04X at offset %u would be tag #%d",
                           ttmResource->resName, ttmSlot->numTags, opcode,
                           opcodeOffset, tagNo + 1);

            if (!peekHasBytes(ttmSlot->dataSize, offset, 2)) {
                fprintf(stderr, "Warning : TTM %s: tag opcode %04X at offset %u has no "
                                "tag id before the end of the script\n",
                        ttmResource->resName, opcode, opcodeOffset);
                break;
            }

            uint16 arg = peekUint16(ttmSlot->data, ttmSlot->dataSize, &offset,
                                    ttmResource->resName);
            ttmSlot->tags[tagNo].id     = arg;
            ttmSlot->tags[tagNo].offset = offset;
            tagNo++; // TODO
        }
        else {

            uint8 numArgs = (uint8)(opcode & 0x000f);

            if (numArgs == 0x0f) {
                /*  BOUNDED. This walk had no size at all: it read data[offset+1]
                 *  one byte past the buffer when the string ended on the last
                 *  byte, and with no zero pair ahead of it it left the
                 *  allocation entirely and kept going. */
                while (peekHasBytes(ttmSlot->dataSize, offset, 2)
                       && ttmSlot->data[offset] != 0
                       && ttmSlot->data[offset+1] != 0)
                    offset += 2;
                offset += 2;
            }
            else {
                offset += ((uint32)numArgs) << 1;
            }
        }
    }

    /*  Observable, because a script that does not decode to its own length is
     *  either truncated or being decoded wrongly, and silently building a short
     *  tag table looks exactly like success. Every shipped TTM lands on its last
     *  byte exactly, so this is quiet in normal operation. Not fatal: the VM
     *  refuses to execute past the end anyway, and the tags found so far are
     *  still usable. */
    if (offset != ttmSlot->dataSize)
        fprintf(stderr, "Warning : TTM %s: script does not decode to its own length "
                        "(tag scan stopped at %u of %u bytes)\n",
                ttmResource->resName, offset, ttmSlot->dataSize);

    // TODO : in SASKDATE.TTM, num SET_SCENE != ttmResource->numTags
    while (tagNo < ttmSlot->numTags)
        ttmSlot->tags[tagNo++].id = (uint16)-1;  // Use invalid tag id as sentinel
}


void ttmInitSlot(struct TTtmSlot *ttmSlot)
{
    ttmSlot->data     = NULL;
    ttmSlot->tags     = NULL;
    ttmSlot->numTags  = 0;
    ttmSlot->dataSize = 0;
    for (int i=0; i < MAX_BMP_SLOTS; i++) {
        ttmSlot->numSprites[i] = 0;
        ttmSlot->bmpNames[i]   = NULL;
    }
}


void ttmResetSlot(struct TTtmSlot *ttmSlot)
{
    /*  Actually reset, which this did not previously do.
     *
     *  It freed `tags` and left the pointer at the freed block, and it kept the
     *  old `numTags` and `dataSize`. A slot left in that state describes a
     *  script that is no longer loaded: `data` is NULL while `dataSize` says
     *  otherwise, so anything that trusted the size walked a NULL pointer, and
     *  anything that read `tags` read freed memory.
     *
     *  `data` is not owned by the slot - it points into the loaded resource -
     *  so it is cleared, never freed. `tags` is owned, and free(NULL) is a
     *  no-op, so the unconditional free also covers the case the old guard
     *  missed: data already NULL while tags was still allocated.
     */
    free(ttmSlot->tags);
    ttmSlot->tags     = NULL;
    ttmSlot->data     = NULL;
    ttmSlot->numTags  = 0;
    ttmSlot->dataSize = 0;

    for (int i=0; i < MAX_BMP_SLOTS; i++) {
        grReleaseBmp(ttmSlot, (uint16)i);
    }
}


void ttmPlay(struct TTtmThread *ttmThread)     // TODO
{
    uint8 *data;
    uint32 offset;
    uint16 opcode;
    uint8 numArgs;
    uint16 args[TTM_MAX_ARGS];
    char strArg[256];
    int continueLoop = 1;
    struct TTtmSlot *ttmSlot;


    grDx = ttmDx;
    grDy = ttmDy;

    ttmSlot = ttmThread->ttmSlot;
    offset = ttmThread->ip;
    data = ttmSlot->data;

    while (continueLoop && peekHasBytes(ttmSlot->dataSize, offset, 2)) {

        uint32 opcodeOffset = offset;

        opcode = peekUint16(data, ttmSlot->dataSize, &offset, "TTM script");

        numArgs = (uint8) opcode & 0x0000f;

        if (numArgs == 0x0f) {        // arg is a string

            int i=0;

            while (offset < ttmSlot->dataSize && data[offset] != 0 && i < (int)(sizeof(strArg) - 1))
                strArg[i++] = (char)data[offset++];

            /*  The terminator and the even-length pad byte were read
             *  unconditionally, so a string left unterminated at the very end of
             *  a script read up to two bytes past the buffer. The loop above
             *  stops at dataSize, which is exactly what makes these two reads
             *  the ones that leave it. */
            if (offset >= ttmSlot->dataSize)
                fatalError("TTM script: string argument of opcode %04X at offset %u is "
                           "not terminated before the end of the %u-byte script",
                           opcode, opcodeOffset, ttmSlot->dataSize);

            strArg[i++] = (char)data[offset++];

            if ((i & 0x01) == 0x01) { // always read an even number of uint8s
                if (offset >= ttmSlot->dataSize)
                    fatalError("TTM script: string argument of opcode %04X at offset %u "
                               "has no pad byte before the end of the %u-byte script",
                               opcode, opcodeOffset, ttmSlot->dataSize);
                strArg[i++] = (char)data[offset++];
            }

            /* Both branches int: the second was size_t, so the ternary's common
             * type was unsigned and GCC warned that `i` changed signedness. */
            strArg[i < (int)sizeof(strArg) ? i : (int)sizeof(strArg) - 1] = '\0';
        }
        else {                        // args are numArgs words
            /*  Only the opcode read was guarded, so the arguments of an opcode
             *  sitting near the end of a script were read off the end of the
             *  buffer. Refused here, where the opcode and offset can be named,
             *  rather than left to the reader's generic backstop. */
            if (!peekHasBytes(ttmSlot->dataSize, offset, (uint32)numArgs << 1))
                fatalError("TTM script: opcode %04X at offset %u needs %u argument bytes "
                           "but only %u of the %u-byte script remain",
                           opcode, opcodeOffset, (unsigned)numArgs << 1,
                           (unsigned)(ttmSlot->dataSize - offset), ttmSlot->dataSize);

            peekUint16Block(data, ttmSlot->dataSize, &offset, args, numArgs,
                            TTM_MAX_ARGS, "TTM script");
        }

        switch (opcode) {

            case 0x0080:
                debugMsg("    DRAW_BACKGROUND");
                // Free images slots - see for example tag 11 of GFFFOOD.TTM
                break;

            case 0x0110:
                debugMsg("    PURGE");
                if (ttmThread->sceneTimer)
                    ttmThread->nextGotoOffset = ttmFindPreviousTag(ttmSlot, offset);
                else
                    ttmThread->isRunning = TTM_ENDING;
                break;

            case 0x0FF0:
                debugMsg("    UPDATE");
                continueLoop = 0;
                break;

            case 0x1021:
                debugMsg("    SET_DELAY %d", args[0]);
                ttmThread->timer = ttmThread->delay = (args[0] > 4 ? args[0] : 4);  // TODO ?
                break;

            case 0x1051:
                debugMsg("    SET_BMP_SLOT %d", args[0]);
                if (args[0] >= MAX_BMP_SLOTS) {
                    debugMsg("Warning: SET_BMP_SLOT %u out of range, clamping to %u", args[0], (uint16)(MAX_BMP_SLOTS - 1));
                    ttmThread->selectedBmpSlot = (uint8)(MAX_BMP_SLOTS - 1);
                }
                else {
                    ttmThread->selectedBmpSlot = (uint8)args[0];
                }
                break;

            case 0x1061:
                debugMsg("    SET_PALETTE_SLOT %d", args[0]);
                break;

            case 0x1101:
                debugMsg("    :LOCAL_TAG %d", args[0]);
                break;

            case 0x1111:
                debugMsg("\n    :TAG %d ------------------------", args[0]);
                break;

            case 0x1121:
                // is called before SAVE_IMAGE1, defines the id of the region
                // for further use by CLEAR_SCREEN
                // (see WOULDBE.TTM for a nice example)
                debugMsg("    TTM_UNKNOWN_1 %d", args[0]);
                break;

            case 0x1201:
                // ex TTM_UNKNOWN_2
                debugMsg("    GOTO_TAG %d", args[0]);
                ttmThread->nextGotoOffset = ttmFindTag(ttmSlot, args[0]);
                break;

            case 0x2002:
                debugMsg("    SET_COLORS %d %d", args[0], args[1]);
                ttmThread->fgColor = (uint8)args[0];
                ttmThread->bgColor = (uint8)args[1];
                break;

            case 0x2012:
                // args always == (0,0)
                // at beginning of scenes, near LOAD_IMAGEs
                debugMsg("    SET_FRAME1 %d %d", args[0], args[1]);
                break;

            case 0x2022:
                debugMsg("    TIMER %d %d", args[0], args[1]);
                // Really, really not sure about this formula... but things
                // do work not so bad like that
                ttmThread->delay = ttmThread->timer = (args[0] + args[1]) / 2;
                break;

            case 0x4004:
                debugMsg("    SET_CLIP_ZONE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grSetClipZone(ttmThread->ttmLayer,
                              (sint16)args[0],
                              (sint16)args[1],
                              (sint16)args[2],
                              (sint16)args[3]);
                break;

            case 0x4204:
                debugMsg("    COPY_ZONE_TO_BG %d %d %d %d", args[0], args[1], args[2], args[3]);
                grCopyZoneToBg(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0x4214:
                // defines the zone to be redrawn at each update ?
                // but seems not used in the original
                debugMsg("    SAVE_IMAGE1 %d %d %d %d", args[0], args[1], args[2], args[3]);
                grSaveImage1(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0xA002:
                debugMsg("    DRAW_PIXEL %d %d", args[0], args[1]);
                grDrawPixel(ttmThread->ttmLayer, (sint16)args[0], (sint16)args[1], ttmThread->fgColor);
                break;

            case 0xA054:
                // only once, in GJGULIVR.TTM.txt
                debugMsg("    SAVE_ZONE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grSaveZone(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0xA064:
                // only once, in GJGULIVR.TTM.txt
                debugMsg("    RESTORE_ZONE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grRestoreZone(ttmThread->ttmLayer, args[0], args[1], args[2], args[3]);
                break;

            case 0xA0A4:
                debugMsg("    DRAW_LINE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawLine(ttmThread->ttmLayer,
                           (sint16)args[0],
                           (sint16)args[1],
                           (sint16)args[2],
                           (sint16)args[3],
                           ttmThread->fgColor);
                break;

            case 0xA104:
                debugMsg("    DRAW_RECT %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawRect(ttmThread->ttmLayer,
                           (sint16)args[0],
                           (sint16)args[1],
                           args[2],
                           args[3],
                           ttmThread->fgColor);
                break;

            case 0xA404:
                debugMsg("    DRAW_CIRCLE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawCircle(ttmThread->ttmLayer,
                             (sint16)args[0],
                             (sint16)args[1],
                             args[2],
                             args[3],
                             ttmThread->fgColor,
                             ttmThread->bgColor);
                break;

            case 0xA504:
                debugMsg("    DRAW_SPRITE %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawSprite(ttmThread->ttmLayer,
                             ttmThread->ttmSlot,
                             (sint16)args[0],
                             (sint16)args[1],
                             args[2],
                             args[3]);
                break;

            case 0xA524:
                debugMsg("    DRAW_SPRITE_FLIP %d %d %d %d", args[0], args[1], args[2], args[3]);
                grDrawSpriteFlip(ttmThread->ttmLayer,
                                 ttmThread->ttmSlot,
                                 (sint16)args[0],
                                 (sint16)args[1],
                                 args[2],
                                 args[3]);
                break;

            case 0xA601:
                // arg : indicates the SAVE_IMAGE1 nb to be used ?
                debugMsg("    CLEAR_SCREEN %d", args[0]);
                grClearScreen(ttmThread->ttmLayer);
                break;

            case 0xB606:
                debugMsg("    DRAW_SCREEN %d %d %d %d %d %d", args[0], args[1], args[2], args[3], args[4], args[5]);
                break;

            case 0xC051:
                debugMsg("    PLAY_SAMPLE %d", args[0]);
                soundPlay(args[0]);
                break;

            case 0xF01F:
                debugMsg("    LOAD_SCREEN %s", strArg);
                grLoadScreen(strArg);
                break;

            case 0xF02F:
                debugMsg("    LOAD_IMAGE %s", strArg);
                grLoadBmp(ttmSlot, ttmThread->selectedBmpSlot, strArg);
                break;

            case 0xF05F:
                debugMsg("    LOAD_PALETTE %s", strArg);
                break;
        }

        if (offset >= ttmSlot->dataSize) {
            ttmThread->isRunning = TTM_ENDING;
            continueLoop = 0;
        }
    }

    grDx = ttmDx;
    grDy = ttmDy;
    ttmThread->ip = offset;
}

