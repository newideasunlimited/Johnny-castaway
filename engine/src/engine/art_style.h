#ifndef ART_STYLE_H
#define ART_STYLE_H

#include <stddef.h>
#include "platform.h"

typedef struct TArtStyle {
    const char *id;
    const char *name;
    const char *root;
    int scale;
    /* Only old HD sprites use the opaque A8-00-A8 compatibility key. */
    int legacyColorKey;
} TArtStyle;

int artStyleCount(void);
const TArtStyle *artStyleGet(int index);
const TArtStyle *artStyleFind(const char *id);
const TArtStyle *artStyleCurrent(void);
/* Selection is a startup operation. Unknown identifiers leave it unchanged. */
int artStyleSelect(const char *id);

/* Call after opening the ZIP and parsing the original resource inventory.
 * Does not open a window. HD accepts old archives without an HD manifest.
 * Cartoon requires a nonempty, valid pack and checks declared coverage. */
int artStyleValidatePack(char *error, size_t errorSize);
int artStyleCurrentScale(void);

/* NULL means the caller should decode the original resource at current scale.
 * Present but invalid Cartoon replacements are fatal and name the exact path. */
PlatformSurface *artStyleLoadScreen(const char *name, int width, int height);
PlatformSurface *artStyleLoadSprite(const char *name, int image, int width, int height);
/* The named Cartoon island footprint extends around the original sprite anchor.
 * Legacy-sized surfaces and HD/original fallbacks always return zero offsets. */
void artStyleSpriteOffset(const char *name, int image, int width, int height,
                          int flipped, int *dx, int *dy);
void artStyleReportUsage(void);

#endif
