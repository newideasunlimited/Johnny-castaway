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
#include "resource.h"
#include "uncompress.h"
#include "zipvfs.h"

#define MAX_ADS_RESOURCES 100
#define MAX_BMP_RESOURCES 200
#define MAX_PAL_RESOURCES 1
#define MAX_SCR_RESOURCES 20
#define MAX_TTM_RESOURCES 100


struct TAdsResource *adsResources[MAX_ADS_RESOURCES];
struct TBmpResource *bmpResources[MAX_BMP_RESOURCES];
struct TPalResource *palResources[MAX_PAL_RESOURCES];
struct TScrResource *scrResources[MAX_SCR_RESOURCES];
struct TTtmResource *ttmResources[MAX_TTM_RESOURCES];
int numAdsResources = 0;
int numBmpResources = 0;
int numPalResources = 0;
int numScrResources = 0;
int numTtmResources = 0;

static struct TMapFile mapFile;


static struct TAdsResource *parseAdsResource(FILE *f)
{
    struct TAdsResource *adsResource;
    uint8 *buffer;


    adsResource = safe_malloc(sizeof(struct TAdsResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"VER:",4))
        fatalError("'VER:' string not found while parsing ADS resource");

    free(buffer);

    readUint32(f);  // Version chunk size; metadata is not used by the engine.
    for (int i=0; i<5; i++)
        readUint8(f);  // Consume the fixed version string with EOF checking.

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"ADS:",4))
        fatalError("'ADS:' string not found while parsing ADS resource");

    free(buffer);

    readUint32(f);  // Unused ADS header bytes.

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"RES:",4))
        fatalError("'RES:' string not found while parsing ADS resource");

    free(buffer);

    readUint32(f);  // RES chunk size.
    adsResource->numRes = readUint16(f);

    adsResource->res = safe_malloc(adsResource->numRes * sizeof(struct TAdsRes));

    for (int i=0; i < adsResource->numRes; i++) {
        adsResource->res[i].id = readUint16(f);
        adsResource->res[i].name = getString(f,40);
    }

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"SCR:",4))
        fatalError("'SCR:' string not found while parsing ADS resource");

    free(buffer);

    {
        uint32 rawSize = readUint32(f);
        if (rawSize < 5)
            fatalError("ADS resource: compressed size field too small (%u)", rawSize);
        adsResource->compressedSize = rawSize - 5;
    }
    adsResource->compressionMethod = readUint8(f);
    adsResource->uncompressedSize = readUint32(f);

    adsResource->uncompressedData = uncompress(f,
                                      adsResource->compressionMethod,
                                      adsResource->compressedSize,
                                      adsResource->uncompressedSize
                                    );
    if (adsResource->uncompressedData == NULL)
        fatalError("Failed to decompress ADS resource data (method=%d)", adsResource->compressionMethod);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TAG:",4))
        fatalError("'TAG:' string not found while parsing ADS resource");

    free(buffer);

    readUint32(f);  // TAG chunk size.
    adsResource->numTags = readUint16(f);

    adsResource->tags = safe_malloc(adsResource->numTags * sizeof(struct TTags));

    for (int i=0; i < adsResource->numTags; i++) {
        adsResource->tags[i].id = readUint16(f);
        adsResource->tags[i].description = getString(f,40);
    }

    return adsResource;
}


static struct TBmpResource *parseBmpResource(FILE *f)
{
    struct TBmpResource *bmpResource;
    uint8 *buffer;


    bmpResource = safe_malloc(sizeof(struct TBmpResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"BMP:",4))
        fatalError("'BMP:' string not found while parsing BMP resource");

    free(buffer);

    readUint16(f);  // Header dimensions; each image has its own dimensions below.
    readUint16(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"INF:",4))
        fatalError("'INF:' string not found while parsing BMP resource");

    free(buffer);

    readUint32(f);  // INF chunk size.
    bmpResource->numImages = readUint16(f);

    bmpResource->widths = readUint16Block(f, bmpResource->numImages);
    bmpResource->heights = readUint16Block(f, bmpResource->numImages);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"BIN:",4))
        fatalError("'BIN:' string not found while parsing BMP resource");

    free(buffer);

    {
        uint32 rawSize = readUint32(f);
        if (rawSize < 5)
            fatalError("BMP resource: compressed size field too small (%u)", rawSize);
        bmpResource->compressedSize = rawSize - 5; // discard size of compressionmethod+uncompressedsize
    }
    bmpResource->compressionMethod = readUint8(f);
    bmpResource->uncompressedSize = readUint32(f);

    bmpResource->uncompressedData = uncompress(f,
                                      bmpResource->compressionMethod,
                                      bmpResource->compressedSize,
                                      bmpResource->uncompressedSize
                                    );
    if (bmpResource->uncompressedData == NULL)
        fatalError("Failed to decompress BMP resource data (method=%d)", bmpResource->compressionMethod);

    return bmpResource;
}


static struct TPalResource *parsePalResource(FILE *f)
{
    struct TPalResource *palResource;
    uint8 *buffer;


    palResource = safe_malloc(sizeof(struct TPalResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"PAL:",4))
        fatalError("'PAL:' string not found while parsing PAL resource");

    free(buffer);

    readUint16(f);  // Palette header size.
    readUint8(f);   // Unused palette header bytes.
    readUint8(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"VGA:",4))
        fatalError("'VGA:' string not found while parsing PAL resource");

    free(buffer);

    readUint8(f);   // size ?
    readUint8(f);
    readUint8(f);
    readUint8(f);

    for (int i=0; i < 256; i++) {
        palResource->colors[i].r = readUint8(f);
        palResource->colors[i].g = readUint8(f);
        palResource->colors[i].b = readUint8(f);
    }

    return palResource;
}


static struct TScrResource *parseScrResource(FILE *f)
{
    struct TScrResource *scrResource;
    uint8 *buffer;


    scrResource = safe_malloc(sizeof(struct TScrResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"SCR:",4))
        fatalError("'SCR:' string not found while parsing SCR resource");

    free(buffer);

    readUint16(f);  // Screen header size.
    readUint16(f);  // Unused flags.

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"DIM:",4))
        fatalError("'DIM:' string not found while parsing SCR resource");

    free(buffer);

    readUint32(f);  // DIM chunk size.
    scrResource->width = readUint16(f);
    scrResource->height = readUint16(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"BIN:",4))
        fatalError("'BIN:' string not found while parsing SCR resource");

    free(buffer);

    {
        uint32 rawSize = readUint32(f);
        if (rawSize < 5)
            fatalError("SCR resource: compressed size field too small (%u)", rawSize);
        scrResource->compressedSize = rawSize - 5; // discard size of compressionmethod+uncompressedsize
    }
    scrResource->compressionMethod = readUint8(f);
    scrResource->uncompressedSize = readUint32(f) ;

    scrResource->uncompressedData = uncompress(f,
                                      scrResource->compressionMethod,
                                      scrResource->compressedSize,
                                      scrResource->uncompressedSize
                                    );
    if (scrResource->uncompressedData == NULL)
        fatalError("Failed to decompress SCR resource data (method=%d)", scrResource->compressionMethod);

    return scrResource;
}


static struct TTtmResource *parseTtmResource(FILE *f)
{
    struct TTtmResource *ttmResource;
    uint8 *buffer;

    ttmResource = safe_malloc(sizeof(struct TTtmResource));

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"VER:",4))
        fatalError("'VER:' string not found while parsing TTM resource");

    free(buffer);

    readUint32(f);  // Version chunk size.
    for (int i=0; i<5; i++)
        readUint8(f);  // Consume the fixed version string with EOF checking.

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"PAG:",4))
        fatalError("'PAG:' string not found while parsing TTM resource");

    free(buffer);

    readUint32(f);  // Page count; playback uses tags scanned from the script.
    readUint8(f);   // Unused PAG header bytes.
    readUint8(f);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TT3:",4))
        fatalError("'TT3:' string not found while parsing TTM resource");

    free(buffer);

    {
        uint32 rawSize = readUint32(f);
        if (rawSize < 5)
            fatalError("TTM resource: compressed size field too small (%u)", rawSize);
        ttmResource->compressedSize = rawSize - 5; // discard size of compressionmethod+uncompressedsize
    }
    ttmResource->compressionMethod = readUint8(f);
    ttmResource->uncompressedSize = readUint32(f);

    ttmResource->uncompressedData = uncompress(f,
                                      ttmResource->compressionMethod,
                                      ttmResource->compressedSize,
                                      ttmResource->uncompressedSize
                                    );
    if (ttmResource->uncompressedData == NULL)
        fatalError("Failed to decompress TTM resource data (method=%d)", ttmResource->compressionMethod);

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TTI:",4))
        fatalError("'TTI:' string not found while parsing TTM resource");

    free(buffer);

    readUint32(f);  // Unused TTI header bytes.

    buffer = readUint8Block(f,4);
    if (memcmp(buffer,"TAG:",4))
        fatalError("'TAG:' string not found while parsing TTM resource");

    free(buffer);

    readUint32(f);  // TAG chunk size.
    ttmResource->numTags = readUint16(f);

    ttmResource->tags = safe_malloc(ttmResource->numTags * sizeof(struct TTags));

    for (int i=0; i < ttmResource->numTags; i++) {
        ttmResource->tags[i].id = readUint16(f);
        ttmResource->tags[i].description = getString(f,40);
    }

    return ttmResource;
}


static void parseMapFile(const char *fileName)
{
    FILE *f_map;

    f_map = zipvfs_fopen(fileName);

    if (f_map == NULL)
        fatalError("Resources map file not found in zip: %s\n", fileName);

    for (int i=0; i<6; i++)
        readUint8(f_map);  // Unused map header bytes; still validate their presence.

    mapFile.resFileName = (char *) getString(f_map,13);

    mapFile.numEntries = readUint16(f_map);

    mapFile.Entries = safe_malloc(mapFile.numEntries * sizeof(struct TMapFileEntry));

    for (int i=0; i<mapFile.numEntries; i++) {
        readUint32(f_map);  // Unused index metadata; resource offsets follow.
        mapFile.Entries[i].offset = readUint32(f_map);
    }

    fclose(f_map);
}


static void parseResourceFile(void)
{
    FILE *f;
    char filepath[256];

    snprintf(filepath, sizeof(filepath), "data/%s", mapFile.resFileName);

    f = zipvfs_fopen(filepath);

    if (f == NULL)
        fatalError("Main resources file not found in zip: %s\n", mapFile.resFileName);

    if (debugMode) {
        printf("Loading resources ");
        fflush (stdout);
    }

    for (int i=0; i < mapFile.numEntries; i++) {

        fseek(f, mapFile.Entries[i].offset, SEEK_SET);

        {
            uint8 *rawName = readUint8Block(f, 13);
            // Ensure null-termination for use with strlen/strcmp
            char *safeName = safe_malloc(14);
            memcpy(safeName, rawName, 13);
            safeName[13] = '\0';
            free(rawName);
            mapFile.Entries[i].resName = safeName;
        }
        readUint32(f);  // Resource entry size; individual chunks are parsed below.

        char *resName = mapFile.Entries[i].resName;

        /*  The name is 13 bytes copied out of the file, so its length is
         *  anything from 0 to 13 - and `resName + strlen(resName) - 4` walks
         *  BEFORE the allocation for any name shorter than its own type suffix,
         *  with an empty name landing four bytes behind the block. That pointer
         *  was then handed straight to strcmp. Every shipped name is 8 to 12
         *  characters, so nothing in the archive reaches it. */
        size_t resNameLen = strlen(resName);

        if (resNameLen < 4)
            fatalError("RESOURCE entry %d has a %zu-character name ('%s'); every entry "
                       "must end in a 4-character type suffix such as '.TTM'",
                       i, resNameLen, resName);

        char *resType = resName + resNameLen - 4;  // get the extension .BMP .ADS etc.

        if (debugMode) {
             putchar('.');
             fflush(stdout);
        }

        if (!strcmp(resType, ".ADS")) {
            if (numAdsResources >= MAX_ADS_RESOURCES)
                fatalError("Too many ADS resources (max %d)", MAX_ADS_RESOURCES);
            adsResources[numAdsResources] = parseAdsResource(f);
            adsResources[numAdsResources]->resName = resName;
            numAdsResources++;
        }
        else if (!strcmp(resType, ".BMP")) {
            if (numBmpResources >= MAX_BMP_RESOURCES)
                fatalError("Too many BMP resources (max %d)", MAX_BMP_RESOURCES);
            bmpResources[numBmpResources] = parseBmpResource(f);
            bmpResources[numBmpResources]->resName = resName;
            numBmpResources++;
        }
        else if (!strcmp(resType, ".PAL")) {
            if (numPalResources >= MAX_PAL_RESOURCES)
                fatalError("Too many PAL resources (max %d)", MAX_PAL_RESOURCES);
            palResources[numPalResources] = parsePalResource(f);
            palResources[numPalResources]->resName = resName;
            numPalResources++;
        }
        else if (!strcmp(resType, ".SCR")) {
            if (numScrResources >= MAX_SCR_RESOURCES)
                fatalError("Too many SCR resources (max %d)", MAX_SCR_RESOURCES);
            scrResources[numScrResources] = parseScrResource(f);
            scrResources[numScrResources]->resName = resName;
            numScrResources++;
        }
        else if (!strcmp(resType, ".TTM")) {
            if (numTtmResources >= MAX_TTM_RESOURCES)
                fatalError("Too many TTM resources (max %d)", MAX_TTM_RESOURCES);
            ttmResources[numTtmResources] = parseTtmResource(f);
            ttmResources[numTtmResources]->resName = resName;
            numTtmResources++;
        }
        // Note: there is one .VIN type file too (FILES.VIN)
        // We dont process it since it's nothing else than a list
        // of files, which we dont need
    }

    fclose(f);

    if (debugMode)
        putchar('\n');
}


void parseResourceFiles(const char *filename)
{
    parseMapFile(filename);
    parseResourceFile();
}


struct TAdsResource *findAdsResource(const char *searchString)
{
    struct TAdsResource *result = NULL;

    for (int i=0; i < numAdsResources && result == NULL; i++) {
        if (!strcmp(adsResources[i]->resName, searchString))
            result = adsResources[i];
    }

    if (result == NULL)
        fatalError("ADS resource %s not found.", searchString);

    return result;
}


struct TBmpResource *findBmpResource(const char *searchString)
{
    struct TBmpResource *result = NULL;

    for (int i=0; i < numBmpResources && result == NULL; i++) {
        if (!strcmp(bmpResources[i]->resName, searchString))
            result = bmpResources[i];
    }

    if (result == NULL)
        fatalError("BMP resource %s not found.", searchString);

    return result;
}


struct TScrResource *findScrResource(const char *searchString)
{
    struct TScrResource *result = NULL;

    for (int i=0; i < numScrResources && result == NULL; i++) {
        if (!strcmp(scrResources[i]->resName, searchString))
            result = scrResources[i];
    }

    if (result == NULL)
        fatalError("SCR resource %s not found.", searchString);

    return result;
}


struct TTtmResource *findTtmResource(const char *searchString)
{
    struct TTtmResource *result = NULL;

    for (int i=0; i < numTtmResources && result == NULL; i++) {
        if (!strcmp(ttmResources[i]->resName, searchString))
            result = ttmResources[i];
    }

    if (result == NULL)
        fatalError("TTM resource %s not found.", searchString);

    return result;
}

