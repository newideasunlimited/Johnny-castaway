/*
 *  Zip Virtual Filesystem for Johnny Reborn
 *
 *  Provides transparent read access to game assets stored inside a zip archive.
 *  Uses miniz for zip decompression.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#if defined(__EMSCRIPTEN__)
    /* Assets are preloaded into the virtual filesystem; there is no meaningful
     * executable directory to probe, so the given path is the only candidate. */
#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
   /* _open/_fdopen and their flags, for the atomic temp-file creation in
    * zipvfs_fopen. errno is read there to tell "that name is taken, draw
    * another" apart from failures that will not improve on a retry. */
#  include <errno.h>
#  include <fcntl.h>
#  include <io.h>
#  include <sys/stat.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

#include "miniz.h"
#include "miniz_zip.h"
#include "mytypes.h"
#include "utils.h"
#include "zipvfs.h"


#define ZIPVFS_MAX_PATH   1024
/*  A candidate holds an executable directory, a separator, a subdirectory such
 *  as "/../assets/", and the caller's name. Sizing it strictly larger than the
 *  parts means truncation cannot happen, which keeps GCC's -Wformat-truncation
 *  quiet for a real reason rather than by suppressing the diagnostic. */
#define ZIPVFS_CAND_PATH  (ZIPVFS_MAX_PATH * 2 + 32)
#define ZIPVFS_MAX_TRIED  5


static mz_zip_archive g_zip;
static int g_zipInitialized = 0;


/*  Directory containing this executable, with no trailing separator.
 *
 *  Returns 1 on success, 0 when the platform cannot tell us (which is not an
 *  error: the caller simply falls back to the current working directory).
 */
static int zipvfs_exeDir(char *buf, size_t bufSize)
{
#if defined(__EMSCRIPTEN__)
    (void)buf;
    (void)bufSize;
    return 0;
#else
    size_t len;

#  if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)bufSize);
    /* n == bufSize means truncated; GetModuleFileNameA does not always set the
     * last error on truncation, so the length test is the reliable check. */
    if (n == 0 || (size_t)n >= bufSize)
        return 0;
    len = (size_t)n;
#  elif defined(__APPLE__)
    uint32_t n = (uint32_t)bufSize;
    if (_NSGetExecutablePath(buf, &n) != 0)
        return 0;
    buf[bufSize - 1] = '\0';
    len = strlen(buf);
#  else
    ssize_t n = readlink("/proc/self/exe", buf, bufSize - 1);
    if (n <= 0 || (size_t)n >= bufSize - 1)
        return 0;
    buf[n] = '\0';
    len = (size_t)n;
#  endif

    /* Strip the filename. Both separators are checked because a Windows path
     * can legitimately contain either. */
    while (len > 0) {
        len--;
        if (buf[len] == '/' || buf[len] == '\\') {
            buf[len] = '\0';
            return 1;
        }
    }

    return 0;   /* no separator at all: a bare name, so no directory to give */
#endif
}


void zipvfs_init(const char *zipPath)
{
    char candidates[ZIPVFS_MAX_TRIED][ZIPVFS_CAND_PATH];
    char exeDir[ZIPVFS_MAX_PATH];
    int numCandidates = 0;
    int i;

    if (g_zipInitialized) {
        debugMsg("zipvfs_init: already initialized, shutting down first");
        zipvfs_shutdown();
    }

    /*  SEARCH, rather than trusting the current directory.
     *
     *  The old code passed zipPath straight to miniz, so the archive had to sit
     *  in whatever directory the process happened to start in. That made the
     *  program unrunnable as built: the zip lives in assets/, nothing copied it
     *  next to the binary, and the Visual Studio debugger starts in the repo
     *  root. Its own error message already promised "the same directory as the
     *  executable", which was never what the code did.
     *
     *  Order is deliberate: an explicit or CWD-relative path still wins, so a
     *  caller can point at a specific archive, and only then do we fall back to
     *  locations relative to the executable.
     */
    /*  JC_REBORN_DATA names the archive outright. An embedding host (the macOS
     *  screen saver) ships the zip inside its own bundle, where neither the
     *  working directory nor the host executable's directory point at it. */
    {
        const char *override = getenv("JC_REBORN_DATA");
        if (override && *override)
            snprintf(candidates[numCandidates++], ZIPVFS_CAND_PATH, "%s", override);
    }

    snprintf(candidates[numCandidates++], ZIPVFS_CAND_PATH, "%s", zipPath);

    if (zipvfs_exeDir(exeDir, sizeof(exeDir))) {
        snprintf(candidates[numCandidates++], ZIPVFS_CAND_PATH,
                 "%s/%s", exeDir, zipPath);
        /* Running straight out of a build tree, where the archive is still in
         * the source layout rather than beside the binary. */
        snprintf(candidates[numCandidates++], ZIPVFS_CAND_PATH,
                 "%s/assets/%s", exeDir, zipPath);
        snprintf(candidates[numCandidates++], ZIPVFS_CAND_PATH,
                 "%s/../assets/%s", exeDir, zipPath);
    }

    for (i = 0; i < numCandidates; i++) {

        memset(&g_zip, 0, sizeof(g_zip));

        if (mz_zip_reader_init_file(&g_zip, candidates[i], 0)) {
            g_zipInitialized = 1;
            debugMsg("zipvfs: opened archive '%s' (%u entries)",
                     candidates[i],
                     (unsigned)mz_zip_reader_get_num_files(&g_zip));
            return;
        }
    }

    /*  Report every path tried. "Failed to open scrantic_data.zip" on its own
     *  cannot be acted on, because it never says where the program looked. */
    {
        char msg[ZIPVFS_MAX_TRIED * ZIPVFS_CAND_PATH + 256];
        int off = snprintf(msg, sizeof(msg),
                           "Failed to open zip archive: %s\n"
                           "Looked in %d location(s):", zipPath, numCandidates);

        for (i = 0; i < numCandidates && off > 0 && (size_t)off < sizeof(msg); i++)
            off += snprintf(msg + off, sizeof(msg) - (size_t)off,
                            "\n    %s", candidates[i]);

        fatalError("%s\nPlace scrantic_data.zip next to the executable, or run "
                   "from a directory that contains it.", msg);
    }
}


FILE *zipvfs_fopen(const char *entryPath)
{
    if (!g_zipInitialized)
        fatalError("zipvfs_fopen: zip VFS not initialized");

    int index = mz_zip_reader_locate_file(&g_zip, entryPath, NULL, 0);
    if (index < 0)
        return NULL;

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&g_zip, (mz_uint)index, &stat))
        return NULL;

    size_t size = (size_t)stat.m_uncomp_size;
    void *data = mz_zip_reader_extract_to_heap(&g_zip, (mz_uint)index, &size, 0);
    if (!data)
        return NULL;

    /* Create a temporary file and write the decompressed data into it.
     * This gives us a real seekable FILE* that the existing resource
     * parsing code can use without any changes. */
    FILE *f = NULL;
#if defined(_WIN32)
    {
        /*  CREATION MUST BE ATOMIC.
         *
         *  This used to be _tempnam followed by fopen(path, "w+bTD"). _tempnam
         *  only chooses a NAME - it does not create anything - and "w+b" is
         *  create-OR-TRUNCATE. So in the window between the two calls, anything
         *  that plants a file at that path has it silently truncated and then
         *  used as this process's resource cache. Two instances sharing %TMP%
         *  collide the same way, which is not hypothetical: the smoke suite runs
         *  several at once.
         *
         *  _O_EXCL closes the window by making creation fail if the path already
         *  exists. _O_TEMPORARY preserves the delete-on-close that the "D" mode
         *  flag provided, and _O_SHORT_LIVED the "T" - keep both, or the temp
         *  files start surviving the process.
         */
        for (int attempt = 0; attempt < 8 && f == NULL; attempt++) {

            char *tmpPath = _tempnam(NULL, "jcr");
            if (!tmpPath)
                break;

            int fd = _open(tmpPath,
                           _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY |
                           _O_TEMPORARY | _O_SHORT_LIVED,
                           _S_IREAD | _S_IWRITE);
            int err = errno;
            free(tmpPath);

            if (fd != -1) {
                f = _fdopen(fd, "w+b");
                if (!f)
                    _close(fd);   /* _O_TEMPORARY still unlinks it */
            }
            else if (err != EEXIST) {
                /* Somebody else owning that name is worth another draw; a
                 * permission or out-of-handles failure is not going to improve
                 * on a retry, so stop rather than spin. */
                break;
            }
        }
    }
#else
    f = tmpfile();
#endif
    if (!f) {
        free(data);
        /* The old text said "tmpfile() failed" on the Windows path, which never
         * called tmpfile - it would have sent whoever triaged it looking at the
         * wrong function entirely. */
        fatalError("zipvfs_fopen: could not create a temporary file to unpack '%s'",
                   entryPath);
    }

    if (fwrite(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        fatalError("zipvfs_fopen: failed to write to temp file");
    }

    free(data);
    rewind(f);
    return f;
}


uint8 *zipvfs_read(const char *entryPath, size_t *outSize)
{
    if (!g_zipInitialized)
        fatalError("zipvfs_read: zip VFS not initialized");

    int index = mz_zip_reader_locate_file(&g_zip, entryPath, NULL, 0);
    if (index < 0)
        return NULL;

    size_t size = 0;
    void *data = mz_zip_reader_extract_to_heap(&g_zip, (mz_uint)index, &size, 0);
    if (!data)
        return NULL;

    if (outSize)
        *outSize = size;

    return (uint8 *)data;
}


int zipvfs_exists(const char *entryPath)
{
    if (!g_zipInitialized)
        return 0;

    return mz_zip_reader_locate_file(&g_zip, entryPath, NULL, 0) >= 0;
}


void zipvfs_shutdown(void)
{
    if (g_zipInitialized) {
        mz_zip_reader_end(&g_zip);
        g_zipInitialized = 0;
    }
}
