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

#ifndef RESOURCE_H
#define RESOURCE_H

//------------------------
//    Types definitions
//------------------------

struct TMapFile {
    char *resFileName;
    uint16 numEntries;
    struct TMapFileEntry *Entries;
};


struct TMapFileEntry {
    uint32 offset;
    char *resName;
};


struct TAdsResource {
    char *resName;
    uint16 numRes;  // TODO rename
    struct TAdsRes *res;  // TODO rename
    uint32 compressedSize;
    uint8 compressionMethod;
    uint32 uncompressedSize;
    uint8 *uncompressedData;
    uint16 numTags;
    struct TTags *tags;
};


struct TAdsRes {
    uint16 id;
    char *name;
};


struct TBmpResource {
    char *resName;
    uint16 numImages;
    uint16 *widths;   // TODO
    uint16 *heights;
    uint32 compressedSize;
    uint8 compressionMethod;
    uint32 uncompressedSize;
    uint8 *uncompressedData;
};


struct TColor {
     uint8 r;
     uint8 g;
     uint8 b;
};


struct TPalResource {
    char *resName;
    struct TColor colors[256];
};


struct TScrResource {
    char *resName;
    uint16 width;
    uint16 height;
    uint32 compressedSize;
    uint8 compressionMethod;
    uint32 uncompressedSize;
    uint8 *uncompressedData;
};


struct TTtmResource {
    char *resName;
    uint32 compressedSize;
    uint8 compressionMethod;
    uint32 uncompressedSize;
    uint8 *uncompressedData;
    uint16 numTags;
    struct TTags *tags; // TODO : merge with ttmTags from ttm.c ?
};


struct TTags {
    uint16 id;
    char *description;
};


//------------------------
//    Public variables
//------------------------

extern struct TAdsResource *adsResources[];
extern struct TBmpResource *bmpResources[];
extern struct TPalResource *palResources[];
extern struct TScrResource *scrResources[];
extern struct TTtmResource *ttmResources[];
extern int numAdsResources;
extern int numBmpResources;
extern int numPalResources;
extern int numScrResources;
extern int numTtmResources;


//----------------------------
//    Functions prototypes
//----------------------------

void parseResourceFiles(const char *);
struct TAdsResource *findAdsResource(const char *searchString);
struct TBmpResource *findBmpResource(const char *searchString);
struct TScrResource *findScrResource(const char *searchString);
struct TTtmResource *findTtmResource(const char *searchString);

#endif
