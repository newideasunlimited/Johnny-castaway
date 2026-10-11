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
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include "mytypes.h"
#include "utils.h"
#include "config.h"
#include "art_style.h"

#define BUFFER_LEN 100


static char *cfgFullPath(void)
{
    char *home;
    static char *result = NULL;


    if (result == NULL) {

        home = getenv("HOME");

#ifdef _WIN32
        if (home == NULL || !strlen(home)) {
            home = getenv("USERPROFILE");
        }
#endif

        if (home != NULL) {
            if (strlen(home)) {
                result = safe_malloc(strlen(home) + strlen(CFG_FILENAME) + 2);
                strcpy(result, home);
                strcat(result, "/");
                strcat(result, CFG_FILENAME);
            }
        }

        if (result == NULL) {
            result = safe_malloc(strlen(CFG_FILENAME) + 1);
            strcpy(result, CFG_FILENAME);
        }
    }

    return result;
}


int cfgFileWrite(const struct TConfig *cfg)
{
    /* Write beside the destination and replace it only after a complete close.
     * A failed save must leave the previous story progress and style intact. */
    const char *path = cfgFullPath();
    size_t capacity = strlen(path) + 40;
    char *temporary = safe_malloc(capacity);
#ifdef _WIN32
    unsigned long processId = (unsigned long)_getpid();
#else
    unsigned long processId = (unsigned long)getpid();
#endif
    snprintf(temporary, capacity, "%s.%lu.tmp", path, processId);
    FILE *f = fopen(temporary, "wx");
    int ok = 0;
    if (f) {
        const char *style = artStyleFind(cfg->artStyle) ? cfg->artStyle : "hd";
        int written = fprintf(f, "currentDay=%d\ndate=%d\nartStyle=%s\n",
                              cfg->currentDay, cfg->date, style);
        int closed = fclose(f);
        if (written >= 0 && closed == 0) {
#ifdef _WIN32
            ok = MoveFileExA(temporary, path,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            ok = rename(temporary, path) == 0;
#endif
        }
        if (!ok) remove(temporary);
    }
    if (!ok) fprintf(stderr, "Could not save %s; previous settings retained\n", CFG_FILENAME);
    free(temporary);
    return ok;
}


void cfgFileRead(struct TConfig *cfg)
{
    char buf[BUFFER_LEN];

    cfg->currentDay = 0;
    cfg->date       = 0;
    strcpy(cfg->artStyle, "hd");

    FILE *f = fopen(cfgFullPath(), "r");

    if (f != NULL) {

        while (fgets(buf, BUFFER_LEN, f) != NULL) {
            size_t length = strcspn(buf, "\r\n");
            if (length == sizeof(buf) - 1 && !feof(f)) {
                int c;
                while ((c = fgetc(f)) != '\n' && c != EOF) { }
                if (!strncmp(buf, "artStyle=", 9)) {
                    strcpy(cfg->artStyle, "hd");
                    fprintf(stderr, "Invalid saved art style; using hd\n");
                }
                continue;
            }
            buf[length] = '\0';

            if(strstr(buf, "currentDay=") == buf)
                cfg->currentDay = atoi(buf + 11);

            if(strstr(buf, "date=") == buf)
                cfg->date = atoi(buf + 5);

            if (!strncmp(buf, "artStyle=", 9)) {
                if (artStyleFind(buf + 9))
                    strcpy(cfg->artStyle, buf + 9);
                else {
                    strcpy(cfg->artStyle, "hd");
                    fprintf(stderr, "Invalid saved art style; using hd\n");
                }
            }
        }

        fclose(f);
    }
}

