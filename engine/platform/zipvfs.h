/*
 *  Zip Virtual Filesystem for Johnny Reborn
 *
 *  Provides transparent read access to game assets stored inside a zip archive.
 *  Uses miniz for zip decompression.
 */

#ifndef ZIPVFS_H
#define ZIPVFS_H

#include <stdio.h>
#include <stddef.h>
#include "mytypes.h"

/*
 * Initialize the zip VFS by opening the given zip archive.
 * Must be called before any other zipvfs_* function.
 * Calls fatalError() on failure.
 */
void zipvfs_init(const char *zipPath);

/*
 * Open a file from the zip archive and return a seekable FILE*.
 * The returned FILE* behaves like a normal file opened with fopen("rb").
 * Caller must fclose() the returned handle when done.
 * Returns NULL if the entry is not found in the zip.
 */
FILE *zipvfs_fopen(const char *entryPath);

/*
 * Read an entire file from the zip archive into a newly allocated buffer.
 * Caller must free() the returned buffer when done.
 * On success, *outSize is set to the uncompressed size.
 * Returns NULL if the entry is not found.
 */
uint8 *zipvfs_read(const char *entryPath, size_t *outSize);

/*
 * Check whether a given entry exists in the zip archive.
 * Returns 1 if found, 0 otherwise.
 */
int zipvfs_exists(const char *entryPath);

/*
 * Shut down the zip VFS and release all resources.
 */
void zipvfs_shutdown(void);

#endif /* ZIPVFS_H */
