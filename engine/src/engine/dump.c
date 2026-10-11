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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "mytypes.h"
#include "resource.h"
#include "utils.h"   // TODO

#define MAX_FILENAME_LEN 2048

char dumpDir[] = "./dump/";
char dumpAdsDir[] = "ADS/";
char dumpBmpDir[] = "BMP/";
char dumpScrDir[] = "SCR/";
char dumpTtmDir[] = "TTM/";


static void safe_mkdir(char *dirname)
{
    struct stat st = {0};
    int res=0;


    if (!stat(dirname, &st)) {
        return;
    }
    else {
#ifdef _WIN32
        res = _mkdir(dirname);
#else
        res = mkdir(dirname, 0755);
#endif
    }

    if (res)
        fatalError("couldn't create directory %s", dirname);
}


/* Only ever called from this file (below), so it does not need external
 * linkage. Narrowed rather than removed: it is live, unlike its neighbours. */
static void createDumpDirs(void)
{
    char dirname[MAX_FILENAME_LEN];


    safe_mkdir(dumpDir);

    snprintf(dirname, MAX_FILENAME_LEN, "%s/%s", dumpDir, dumpAdsDir);
    safe_mkdir(dirname);

    snprintf(dirname, MAX_FILENAME_LEN, "%s/%s", dumpDir, dumpBmpDir);
    safe_mkdir(dirname);

    snprintf(dirname, MAX_FILENAME_LEN, "%s/%s", dumpDir, dumpScrDir);
    safe_mkdir(dirname);

    snprintf(dirname, MAX_FILENAME_LEN, "%s/%s", dumpDir, dumpTtmDir);
    safe_mkdir(dirname);
}


static void dumpBmp(struct TBmpResource *bmpResource, struct TPalResource *palResource)
{
    FILE *fout;
    uint8 *data;
    char filename[MAX_FILENAME_LEN];


    printf("---- Dumping BMP : %s (%d images) \n", bmpResource->resName,
                                                   bmpResource->numImages);

    data = bmpResource->uncompressedData;

    size_t srcUsed = 0;

    for (int image=0; image < bmpResource->numImages; image++) {

        if ((bmpResource->widths[image] % 2) == 1) {
            printf("Error : can't manage odd widths\n");
            return;
        }

        /*  The same unbounded pixel walk grLoadBmp had: `data` advances
         *  width/2 bytes per row with nothing checking it against the size that
         *  actually decoded. Shipped BMPs sum to exactly their decoded size. */
        {
            size_t need = ((size_t)bmpResource->widths[image]
                           * (size_t)bmpResource->heights[image]) / 2;

            if (need > (size_t)bmpResource->uncompressedSize - srcUsed)
                fatalError("BMP '%s': image %d (%ux%u) needs %zu pixel bytes at offset "
                           "%zu, but only %u bytes were decoded",
                           bmpResource->resName, image, bmpResource->widths[image],
                           bmpResource->heights[image], need, srcUsed,
                           bmpResource->uncompressedSize);

            srcUsed += need;
        }

        snprintf(filename, MAX_FILENAME_LEN, "%s/%s/%s.%03d.xpm",
                                           dumpDir,
                                           dumpBmpDir,
                                           bmpResource->resName,
                                           image);
        /* BINARY MODE, so the dump is byte-identical on every platform. In text mode
     * the Windows CRT expands every \n to \r\n, so the same decoder produced
     * different bytes on Windows and Linux: 369 CR bytes in one 226 KB XPM, and
     * all 2,452 files differing for that reason alone. That made the dump
     * useless as a cross-platform regression oracle, which is the one job it is
     * uniquely suited to. See tests/Invoke-DumpRegression.ps1. */
    fout = safe_fopen(filename ,"wb");

        uint16 width = bmpResource->widths[image];
        uint16 height = bmpResource->heights[image];

        fputs("/* XPM */\n", fout);
        fputs("static char * scrantic[] = {\n", fout);
        fprintf(fout, "\"%d %d 16 1\",\n", width, height);

        for (int i=0; i < 16; i++)
            fprintf(fout, "\"%1x c #%02x%02x%02x\",\n", i,
                                    palResource->colors[i].r << 2,
                                    palResource->colors[i].g << 2,
                                    palResource->colors[i].b << 2);

        for (int y=0; y < height; y++) {  // TODO

            fputs ("\"", fout);

            for (int x=0; x < (width/2); x++) {
                fprintf(fout, "%02x", data[0]);
                data++;
            }

            if (y == (height-1))
                fputs ("\"}\n", fout);
            else
                fputs ("\",\n", fout);
        }

        fclose(fout);
    }
}


static void dumpScr(struct TScrResource *scrResource, struct TPalResource *palResource)
{
    FILE *fout;
    uint8 *data;
    char filename[MAX_FILENAME_LEN];


    printf("---- Dumping SCR : %s  \n", scrResource->resName);

    if (scrResource->width % 2)
        fatalError("SCR '%s': unsupported odd width %u (packed pixels require an even width)",
                   scrResource->resName, scrResource->width);

    {
        size_t need = ((size_t)scrResource->width / 2) * (size_t)scrResource->height;

        if (need > (size_t)scrResource->uncompressedSize)
            fatalError("SCR '%s': %ux%u needs %zu pixel bytes, but only %u bytes were decoded",
                       scrResource->resName, scrResource->width, scrResource->height,
                       need, scrResource->uncompressedSize);
    }

    snprintf(filename, MAX_FILENAME_LEN, "%s/%s/%s.xpm",
                                      dumpDir,
                                      dumpScrDir,
                                      scrResource->resName);
    /* BINARY MODE, so the dump is byte-identical on every platform. In text mode
     * the Windows CRT expands every \n to \r\n, so the same decoder produced
     * different bytes on Windows and Linux: 369 CR bytes in one 226 KB XPM, and
     * all 2,452 files differing for that reason alone. That made the dump
     * useless as a cross-platform regression oracle, which is the one job it is
     * uniquely suited to. See tests/Invoke-DumpRegression.ps1. */
    fout = safe_fopen(filename ,"wb");

    uint16 width = scrResource->width;
    uint16 height = scrResource->height;

    fputs("/* XPM */\n", fout);
    fputs("static char * scrantic[] = {\n", fout);
    fprintf(fout, "\"%d %d 16 1\",\n", width, height);

    for (int i=0; i < 16; i++)
        fprintf(fout, "\"%1x c #%02x%02x%02x\",\n", i,
                                palResource->colors[i].r << 2,
                                palResource->colors[i].g << 2,
                                palResource->colors[i].b << 2);

    data = scrResource->uncompressedData;

    for (int y=0; y < height; y++) {  // TODO
        fputs ("\"", fout);
        for (int x=0; x < (width/2); x++) {
            fprintf(fout, "%02x", data[0]);
            data++;
        }
        if (y == (height-1))
             fputs ("\"}\n", fout);
         else
             fputs ("\",\n", fout);
    }

    fclose(fout);
}


static void dumpAds(struct TAdsResource *adsResource)
{
    FILE *fout;
    char filename[MAX_FILENAME_LEN];

    uint8 *data;
    uint32 offset;
    uint16 opcode;
    uint8 numArgs;


    printf("---- Dumping ADS : %s  \n", adsResource->resName);

    snprintf(filename, MAX_FILENAME_LEN, "%s/%s/%s.txt",
                                               dumpDir,
                                               dumpAdsDir,
                                               adsResource->resName);
    /* BINARY MODE, so the dump is byte-identical on every platform. In text mode
     * the Windows CRT expands every \n to \r\n, so the same decoder produced
     * different bytes on Windows and Linux: 369 CR bytes in one 226 KB XPM, and
     * all 2,452 files differing for that reason alone. That made the dump
     * useless as a cross-platform regression oracle, which is the one job it is
     * uniquely suited to. See tests/Invoke-DumpRegression.ps1. */
    fout = safe_fopen(filename ,"wb");



    fprintf(fout, "\n ======== Resources list ========\n\n");

    for (int i=0; i < adsResource->numRes; i++)
        fprintf(fout, "%d : %s\n",  adsResource->res[i].id,
                                    adsResource->res[i].name);


    fprintf(fout, "\n\n ======== Tags list ========\n\n");

    for (int i=0; i < adsResource->numTags; i++)
        fprintf(fout, "%d : %s\n",  adsResource->tags[i].id,
                                    adsResource->tags[i].description);


    fprintf(fout, "\n\n ======== ADS script ========\n");

    data = adsResource->uncompressedData;
    offset = 0;

    while (peekHasBytes(adsResource->uncompressedSize, offset, 2)) {

        opcode = peekUint16(data, adsResource->uncompressedSize, &offset,
                            adsResource->resName);

        switch (opcode) {
            case 0x1070: fprintf(fout, "IF_LASTPLAYED_LOCAL"); numArgs=2; break;
            case 0x1330: fprintf(fout, "IF_UNKNOWN_1"); numArgs=2; break;
            case 0x1350: fprintf(fout, "IF_LASTPLAYED"); numArgs=2; break;
            case 0x1360: fprintf(fout, "IF_NOT_RUNNING"); numArgs=2; break;
            case 0x1370: fprintf(fout, "IF_IS_RUNNING"); numArgs=2; break;
            case 0x1420: fprintf(fout, "AND"); numArgs=0; break;
            case 0x1430: fprintf(fout, "OR"); numArgs=0; break;
            case 0x1510: fprintf(fout, "PLAY_SCENE"); numArgs=0; break;
            case 0x1520: fprintf(fout, "ADD_SCENE_LOCAL"); numArgs=5; break;
            case 0x2005: fprintf(fout, "ADD_SCENE"); numArgs=4; break;
            case 0x2010: fprintf(fout, "STOP_SCENE"); numArgs=3; break;
            case 0x2014: fprintf(fout, "UNKNOWN_5"); numArgs=0; break;
            case 0x3010: fprintf(fout, "RANDOM_START"); numArgs=0; break;
            case 0x3020: fprintf(fout, "NOP"); numArgs=1; break;
            case 0x30ff: fprintf(fout, "RANDOM_END"); numArgs=0; break;
            case 0x4000: fprintf(fout, "UNKNOWN_6"); numArgs=3; break;
            case 0xf010: fprintf(fout, "FADE_OUT"); numArgs=0; break;
            case 0xf200: fprintf(fout, "GOSUB_TAG"); numArgs=1; break;
            case 0xffff: fprintf(fout, "END"); numArgs=0; break;
            case 0xfff0: fprintf(fout, "END_IF"); numArgs=0; break;
            default : fprintf(fout, "\n:TAG %d", opcode); numArgs=0; break;
        }

        for (int b=0; b<numArgs; b++)
            fprintf(fout, " %d", peekUint16(data, adsResource->uncompressedSize,
                                            &offset, adsResource->resName));

        fprintf(fout, "\n");
    }

    fclose(fout);
}


static void dumpTtm(struct TTtmResource *ttmResource)
{
    FILE *fout;
    char filename[MAX_FILENAME_LEN];

    uint8 *data;
    uint32 offset;
    uint16 opcode;
    uint8 numArgs;
    uint16 args[20];
    char strArg[256];


    printf("---- Dumping TTM : %s  \n", ttmResource->resName);

    snprintf(filename, MAX_FILENAME_LEN, "%s/%s/%s.txt",
                                      dumpDir,
                                      dumpTtmDir,
                                      ttmResource->resName);

    /* BINARY MODE, so the dump is byte-identical on every platform. In text mode
     * the Windows CRT expands every \n to \r\n, so the same decoder produced
     * different bytes on Windows and Linux: 369 CR bytes in one 226 KB XPM, and
     * all 2,452 files differing for that reason alone. That made the dump
     * useless as a cross-platform regression oracle, which is the one job it is
     * uniquely suited to. See tests/Invoke-DumpRegression.ps1. */
    fout = safe_fopen(filename ,"wb");


    fprintf(fout, "\n ======== Tags list ========\n");

    for (int i=0; i < ttmResource->numTags; i++)
        fprintf(fout, "%d : %s\n", ttmResource->tags[i].id,
                                   ttmResource->tags[i].description);


    fprintf(fout, "\n\n ======== TTM script ========\n");

    data = ttmResource->uncompressedData;
    offset = 0;

    uint32 dataSize = ttmResource->uncompressedSize;

    while (peekHasBytes(dataSize, offset, 2)) {

        uint32 opcodeOffset = offset;

        opcode = peekUint16(data, dataSize, &offset, ttmResource->resName);

        numArgs = (uint8) opcode & 0x0000f;

        if (numArgs == 0x0f) {       // arg is a string

            int i=0;

            /*  This walk had no size bound at all - not even the one ttm.c's
             *  copy had - so an unterminated string ran off the end of the
             *  decompressed buffer until it happened to find a zero. */
            while (offset < dataSize && data[offset] != 0 && i < (int)(sizeof(strArg) - 1))
                strArg[i++] = (char)data[offset++];

            if (offset >= dataSize)
                fatalError("TTM '%s': string argument of opcode %04X at offset %u is not "
                           "terminated before the end of the %u-byte script",
                           ttmResource->resName, opcode, opcodeOffset, dataSize);

            strArg[i++] = (char)data[offset++];

            if ((i & 0x01) == 0x01) {  // always read an even number of uint8s
                if (offset >= dataSize)
                    fatalError("TTM '%s': string argument of opcode %04X at offset %u has "
                               "no pad byte before the end of the %u-byte script",
                               ttmResource->resName, opcode, opcodeOffset, dataSize);
                strArg[i++] = (char)data[offset++];
            }

            /* Both branches int: the second was size_t, so the ternary's common
             * type was unsigned and GCC warned that `i` changed signedness. */
            strArg[i < (int)sizeof(strArg) ? i : (int)sizeof(strArg) - 1] = '\0';
        }
        else {                        // args are numArgs words
            peekUint16Block(data, dataSize, &offset, args, numArgs,
                            (int)(sizeof(args) / sizeof(args[0])), ttmResource->resName);
        }

        switch (opcode) {

            case 0x001f:
                fprintf(fout, "SAVE_BACKGROUND \n");
                break;

            case 0x0080:
                fprintf(fout, "DRAW_BACKGROUND\n");
                break;

            case 0x0110:
                fprintf(fout, "PURGE\n");
                break;

            case 0x0FF0:
                fprintf(fout, "UPDATE\n");
                break;

            case 0x1021:
                fprintf(fout, "SET_DELAY %d\n", args[0]);
                break;

            case 0x1051:
                fprintf(fout, "SET_BMP_SLOT %d\n", args[0]);
                break;

            case 0x1061:
                fprintf(fout, "SET_PALETTE_SLOT %d\n", args[0]);
                break;

            case 0x1101:
                fprintf(fout, ":LOCAL_TAG %d\n", args[0]);
                break;

            case 0x1111:
                fprintf(fout, "\n:TAG %d\n", args[0]);
                break;

            case 0x1121:
                fprintf(fout, "TTM_UNKNOWN_1 %d\n", args[0]);
                break;

            case 0x1201:
                fprintf(fout, "GOTO_TAG %d\n", args[0]);
                break;

            case 0x2002:
                fprintf(fout, "SET_COLORS %d %d\n", args[0], args[1]);
                break;

            case 0x2012:
                fprintf(fout, "SET_FRAME1 %d %d\n", args[0], args[1]);
                break;

            case 0x2022:
                fprintf(fout, "TIMER %d %d\n", args[0], args[1]);
                break;

            case 0x4004:
                fprintf(fout, "SET_CLIP_ZONE %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0x4110:
                fprintf(fout, "FADE_OUT \n");
                break;

            case 0x4120:
                fprintf(fout, "FADE_IN \n");
                break;

            case 0x4204:
                fprintf(fout, "COPY_ZONE_TO_BG %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0x4214:
                fprintf(fout, "SAVE_IMAGE1 %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA002:
                fprintf(fout, "DRAW_PIXEL %d %d\n", args[0], args[1]);
                break;

            case 0xA054:
                fprintf(fout, "SAVE_ZONE %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA064:
                fprintf(fout, "RESTORE_ZONE %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA0A4:
                fprintf(fout, "DRAW_LINE %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA104:
                fprintf(fout, "DRAW_RECT %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA404:
                fprintf(fout, "DRAW_CIRCLE %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA504:
                fprintf(fout, "DRAW_SPRITE %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA510:
                fprintf(fout, "DRAW_SPRITE1 \n");
                break;

            case 0xA524:
                fprintf(fout, "DRAW_SPRITE_FLIP %d %d %d %d\n", args[0], args[1], args[2], args[3]);
                break;

            case 0xA530:
                fprintf(fout, "DRAW_SPRITE3 \n");
                break;

            case 0xA601:
                fprintf(fout, "CLEAR_SCREEN %d\n", args[0]);
                break;

            case 0xB606:
                fprintf(fout, "DRAW_SCREEN %d %d %d %d %d %d\n", args[0], args[1], args[2], args[3], args[4], args[5]);
                break;

            case 0xC020:
                fprintf(fout, "LOAD_SAMPLE \n");
                break;

            case 0xC030:
                fprintf(fout, "SELECT_SAMPLE \n");
                break;

            case 0xC040:
                fprintf(fout, "DESELECT_SAMPLE \n");
                break;

            case 0xC051:
                fprintf(fout, "PLAY_SAMPLE %d\n", args[0]);
                break;

            case 0xC060:
                fprintf(fout, "STOP_SAMPLE \n");
                break;

            case 0xF01F:
                fprintf(fout, "LOAD_SCREEN %s\n", strArg);
                break;

            case 0xF02F:
                fprintf(fout, "LOAD_IMAGE %s\n", strArg);
                break;

            case 0xF05F:
                fprintf(fout, "LOAD_PALETTE %s\n", strArg);
                break;
        }
    }

    fclose(fout);
}


void dumpAllResources(void)
{
    createDumpDirs();

    for (int i = 0; i < numScrResources; i++)
        dumpScr(scrResources[i], palResources[0]);

    for (int i = 0; i < numBmpResources; i++)
        dumpBmp(bmpResources[i], palResources[0]);

    for (int i = 0; i < numAdsResources; i++)
        dumpAds(adsResources[i]);

    for (int i = 0; i < numTtmResources; i++)
        dumpTtm(ttmResources[i]);
}

