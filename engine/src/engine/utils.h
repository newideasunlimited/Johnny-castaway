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

#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "mytypes.h"

extern int debugMode;

/* Portable noreturn attribute — lets callers drop unreachable fallback returns
 * without tripping "control reaches end of non-void function" warnings on
 * compilers that can't infer noreturn from a trailing exit() call. */
#if defined(_MSC_VER)
#define JCR_NORETURN __declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
#define JCR_NORETURN __attribute__((noreturn))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define JCR_NORETURN _Noreturn
#else
#define JCR_NORETURN
#endif

JCR_NORETURN void fatalError(const char *message, ... );

/*  Process exit, routed through one function so an embedding host can
 *  intercept it. The engine was written as a standalone program that leaves
 *  through exit(); inside a macOS screen saver bundle the engine runs on a
 *  worker thread of a host process (legacyScreenSaver), and exit() there would
 *  kill the host. With JC_EMBEDDED defined, jcExit() hands the code to
 *  platformEmbeddedExit(), which the host backend implements (it ends the
 *  engine thread instead of the process). Without it, jcExit() is exit(). */
JCR_NORETURN void jcExit(int code);
#ifdef JC_EMBEDDED
JCR_NORETURN void platformEmbeddedExit(int code);
void platformEmbeddedFatal(const char *message);
#endif
void   debugMsg(const char *message, ... );
void   *safe_malloc(size_t size);
FILE   *safe_fopen(const char *pathname, const char *mode);
uint8  readUint8(FILE *f);
uint16 readUint16(FILE *f);
uint32 readUint32(FILE *f);
char   *getString(FILE *f, int maxlen);
uint8  *readUint8Block(FILE *f, int len);
uint16 *readUint16Block(FILE *f, int len);
/*  BOUNDED SCRIPT READS.
 *
 *  These took no buffer size at all. Every VM loop guarded only the opcode read,
 *  so an opcode near the end of a script read its arguments off the end of the
 *  decompressed buffer - and there is no slack to absorb that: measured over the
 *  shipped archive, all 41 TTM and all 10 ADS scripts decode to EXACTLY their
 *  own last byte.
 *
 *  peekHasBytes() is the guard the decode loops use to stop cleanly; the two
 *  readers refuse (fatalError, naming the resource, the offset and the size)
 *  rather than read past the end, so a truncated script cannot be half-executed.
 *
 *  `what` names the resource in the diagnostic and may be NULL.
 */
int    peekHasBytes(uint32 dataSize, uint32 offset, uint32 nBytes);
uint16 peekUint16(const uint8 *data, uint32 dataSize, uint32 *offset, const char *what);
void   peekUint16Block(const uint8 *data, uint32 dataSize, uint32 *offset,
                       uint16 *dest, int len, int destCapacity, const char *what);
int    getDayOfYear(void);
int    getHour(void);
char   *getMonthAndDay(void);

#endif // UTILS_H
