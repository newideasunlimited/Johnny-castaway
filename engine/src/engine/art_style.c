#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art_style.h"
#include "resource.h"
#include "utils.h"
#include "zipvfs.h"

static const TArtStyle styles[] = {
    { "hd", "HD", "data/hd", 2, 1 },
    { "cartoon", "Cartoon (preview)", "data/styles/cartoon", 2, 0 }
};
static const TArtStyle *selected = &styles[0];
static int validated;
static int currentScale = 1;
static int hdScale = 1;
static unsigned long selectedLoads, hdLoads, originalLoads;
static int usageReported;

int artStyleCount(void) { return (int)(sizeof(styles) / sizeof(styles[0])); }
const TArtStyle *artStyleGet(int index)
{
    return index >= 0 && index < artStyleCount() ? &styles[index] : NULL;
}
const TArtStyle *artStyleFind(const char *id)
{
    if (id)
        for (int i = 0; i < artStyleCount(); i++)
            if (strcmp(id, styles[i].id) == 0) return &styles[i];
    return NULL;
}
const TArtStyle *artStyleCurrent(void) { return selected; }
int artStyleSelect(const char *id)
{
    const TArtStyle *style = artStyleFind(id);
    if (!style) return 0;
    selected = style;
    validated = 0;
    currentScale = 1;
    selectedLoads = hdLoads = originalLoads = 0;
    usageReported = 0;
    return 1;
}
int artStyleCurrentScale(void) { return currentScale; }

/* Preserve the original HD manifest interpretation for existing archives. */
static int readHdScale(void)
{
    size_t size = 0;
    uint8 *data = zipvfs_read("data/hd/manifest.json", &size);
    if (!data) return 1;
    char buf[4097];
    size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
    memcpy(buf, data, n);
    free(data);
    buf[n] = '\0';
    char *p = strstr(buf, "\"scale\"");
    if (!p) p = strstr(buf, "scale");
    if (p) p = strchr(p, ':');
    if (p) {
        p++;
        while (*p && !isdigit((unsigned char)*p)) p++;
        long value = strtol(p, NULL, 10);
        if (value > 1 && value <= 8) return (int)value;
    }
    return 1;
}

/* A bounded JSON reader for the new manifest. Unknown metadata is skipped as
 * JSON, so an occurrence inside an asset list or description cannot satisfy a
 * required top-level property. No JSON library is needed by the renderer. */
typedef struct { const char *p, *end; } Json;
static void jsonSpace(Json *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' ||
           *j->p == '\r' || *j->p == '\n')) j->p++;
}
static int jsonTake(Json *j, char c)
{
    jsonSpace(j);
    if (j->p == j->end || *j->p != c) return 0;
    j->p++;
    return 1;
}
static int jsonString(Json *j, char *out, size_t capacity)
{
    size_t used = 0;
    if (!jsonTake(j, '"')) return 0;
    while (j->p < j->end) {
        unsigned char c = (unsigned char)*j->p++;
        if (c == '"') {
            if (out) out[used] = '\0';
            return 1;
        }
        if (c < 0x20) return 0;
        if (c == '\\') {
            if (j->p == j->end) return 0;
            c = (unsigned char)*j->p++;
            switch (c) {
                case '"': case '\\': case '/': break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    unsigned value = 0;
                    for (int k = 0; k < 4; k++) {
                        if (j->p == j->end) return 0;
                        unsigned char h = (unsigned char)*j->p++;
                        unsigned digit;
                        if (h >= '0' && h <= '9') digit = h - '0';
                        else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
                        else return 0;
                        value = value * 16 + digit;
                    }
                    /* Required identifiers are ASCII. Preserve ASCII escapes,
                     * while non-ASCII metadata can still be skipped. */
                    c = value > 0 && value < 128 ? (unsigned char)value : 0xFF;
                    break;
                }
                default: return 0;
            }
        }
        if (out) {
            if (used + 1 >= capacity) return 0;
            out[used++] = (char)c;
        }
    }
    return 0;
}
static int jsonNumber(Json *j)
{
    jsonSpace(j);
    if (j->p < j->end && *j->p == '-') j->p++;
    if (j->p == j->end) return 0;
    if (*j->p == '0') j->p++;
    else {
        if (*j->p < '1' || *j->p > '9') return 0;
        while (j->p < j->end && isdigit((unsigned char)*j->p)) j->p++;
    }
    if (j->p < j->end && *j->p == '.') {
        j->p++;
        const char *start = j->p;
        while (j->p < j->end && isdigit((unsigned char)*j->p)) j->p++;
        if (j->p == start) return 0;
    }
    if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
        j->p++;
        if (j->p < j->end && (*j->p == '+' || *j->p == '-')) j->p++;
        const char *start = j->p;
        while (j->p < j->end && isdigit((unsigned char)*j->p)) j->p++;
        if (j->p == start) return 0;
    }
    return 1;
}
static int jsonSkip(Json *j, int depth)
{
    jsonSpace(j);
    if (depth > 32 || j->p == j->end) return 0;
    if (*j->p == '"') return jsonString(j, NULL, 0);
    if (*j->p == '{' || *j->p == '[') {
        int object = *j->p++ == '{';
        char close = object ? '}' : ']';
        if (jsonTake(j, close)) return 1;
        do {
            if (object && (!jsonString(j, NULL, 0) || !jsonTake(j, ':'))) return 0;
            if (!jsonSkip(j, depth + 1)) return 0;
            if (jsonTake(j, close)) return 1;
        } while (jsonTake(j, ','));
        return 0;
    }
    const char *words[] = { "true", "false", "null" };
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(words[i]);
        if ((size_t)(j->end - j->p) >= n && memcmp(j->p, words[i], n) == 0) {
            j->p += n;
            return 1;
        }
    }
    return jsonNumber(j);
}
static int readCartoonManifest(const char *data, size_t size, int *complete)
{
    Json j = { data, data + size };
    unsigned seen = 0;
    if (!jsonTake(&j, '{')) return 0;
    do {
        char key[64], value[64];
        unsigned flag = 0;
        if (!jsonString(&j, key, sizeof(key)) || !jsonTake(&j, ':')) return 0;
        if (strcmp(key, "id") == 0) flag = 1;
        else if (strcmp(key, "scale") == 0) flag = 2;
        else if (strcmp(key, "alpha") == 0) flag = 4;
        else if (strcmp(key, "coverage") == 0) flag = 8;
        if (flag && (seen & flag)) return 0;
        seen |= flag;
        if (flag == 2) {
            jsonSpace(&j);
            const char *start = j.p;
            char *end;
            if (!jsonNumber(&j)) return 0;
            long scale = strtol(start, &end, 10);
            if (end != j.p || scale != selected->scale) return 0;
        }
        else if (flag) {
            if (!jsonString(&j, value, sizeof(value))) return 0;
            if (flag == 1 && strcmp(value, selected->id) != 0) return 0;
            if (flag == 4 && strcmp(value, "straight") != 0) return 0;
            if (flag == 8) {
                if (strcmp(value, "complete") == 0) *complete = 1;
                else if (strcmp(value, "partial") != 0) return 0;
            }
        }
        else if (!jsonSkip(&j, 0)) return 0;
        if (jsonTake(&j, '}')) {
            jsonSpace(&j);
            return seen == 15 && j.p == j.end;
        }
    } while (jsonTake(&j, ','));
    return 0;
}

static void assetPath(char *path, size_t capacity, const TArtStyle *style,
                      const char *name, int image)
{
    int n = image < 0
        ? snprintf(path, capacity, "%s/SCR/%s.png", style->root, name)
        : snprintf(path, capacity, "%s/BMP/%s/%03d.png", style->root, name, image);
    if (n < 0 || (size_t)n >= capacity) fatalError("Art asset path is too long: %s", name);
}
int artStyleValidatePack(char *error, size_t errorSize)
{
    if (error && errorSize) error[0] = '\0';
    if (validated) return 1;
    hdScale = readHdScale();
    if (selected == &styles[0]) {
        currentScale = hdScale;
        validated = 1;
        return 1;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/manifest.json", selected->root);
    size_t size = 0;
    uint8 *data = zipvfs_read(path, &size);
    if (!data) {
        if (error && errorSize) snprintf(error, errorSize,
            "Art style '%s' is unavailable: missing %s", selected->id, path);
        return 0;
    }
    int complete = 0;
    /* The numeric reader needs a final NUL for strtol, even for malformed JSON. */
    char *text = NULL;
    if (size && size <= 1024 * 1024) {
        text = safe_malloc(size + 1);
        memcpy(text, data, size);
        text[size] = '\0';
    }
    free(data);
    int valid = text && readCartoonManifest(text, size, &complete);
    free(text);
    if (!valid) {
        if (error && errorSize) snprintf(error, errorSize,
            "Invalid art manifest %s: require id '%s', scale %d, alpha 'straight', "
            "and coverage 'partial' or 'complete'", path, selected->id, selected->scale);
        return 0;
    }
    unsigned available = 0, total = 0;
    for (int i = 0; i < numScrResources; i++) {
        assetPath(path, sizeof(path), selected, scrResources[i]->resName, -1);
        total++;
        if (zipvfs_exists(path)) available++;
    }
    for (int i = 0; i < numBmpResources; i++) {
        for (int frame = 0; frame < bmpResources[i]->numImages; frame++) {
            assetPath(path, sizeof(path), selected, bmpResources[i]->resName, frame);
            total++;
            if (zipvfs_exists(path)) available++;
        }
    }
    if (!available || (complete && available != total)) {
        if (error && errorSize) snprintf(error, errorSize,
            "Invalid art manifest %s/manifest.json: declares %s coverage but supplies %u/%u original assets",
            selected->root, complete ? "complete" : "partial", available, total);
        return 0;
    }
    currentScale = selected->scale;
    validated = 1;
    printf("Art style: %s (%s pack: %u/%u assets%s), scale=%d\n",
        selected->name, complete ? "complete" : "partial", available, total,
        complete ? "" : "; missing art uses HD/original", currentScale);
    return 1;
}

static void releaseLoadedSurface(PlatformSurface *surface)
{
    free(platformGetSurfacePixels(surface));
    platformFreeSurface(surface);
}
/* cartoon-island-ground-v1. Original RESOURCE dimensions remain 280x52.
 * This is one registered footprint, not permission for arbitrary sprite sizes. */
static int extendedIsland(const char *name, int image, int width, int height)
{
    return name && strcmp(name, "BACKGRND.BMP") == 0 && image == 0 &&
           width == 640 && height == 180;
}
static int extendedCenterFoam(const char *name, int image, int width, int height)
{
    return name && strcmp(name, "BACKGRND.BMP") == 0 && image >= 6 && image <= 8 &&
           width == 384 && height == 256;
}
static int extendedLeftFoam(const char *name, int image, int width, int height)
{
    return name && strcmp(name, "BACKGRND.BMP") == 0 && image >= 3 && image <= 5 &&
           width == 150 && height == 66;
}
static int extendedRightFoam(const char *name, int image, int width, int height)
{
    return name && strcmp(name, "BACKGRND.BMP") == 0 && image >= 9 && image <= 11 &&
           width == 154 && height == 74;
}
void artStyleSpriteOffset(const char *name, int image, int width, int height,
                          int flipped, int *dx, int *dy)
{
    *dx = *dy = 0;
    if (selected == &styles[1] && currentScale == 2 &&
        extendedIsland(name, image, width, height)) {
        /* Mirror around the original 560-pixel logical canvas, not the new one. */
        *dx = flipped ? 560 - 640 - (-36) : -36;
        *dy = -10;
    }
    else if (selected == &styles[1] && currentScale == 2 &&
             extendedCenterFoam(name, image, width, height)) {
        /* Preserve the original 320-wide logical anchor when flipping padding. */
        *dx = flipped ? 320 - 384 - (-32) : -32;
        *dy = -90;
    }
    else if (selected == &styles[1] && currentScale == 2 &&
             extendedLeftFoam(name, image, width, height)) {
        *dx = flipped ? 144 - 150 - (-6) : -6;
    }
    else if (selected == &styles[1] && currentScale == 2 &&
             extendedRightFoam(name, image, width, height)) {
        *dx = flipped ? 144 - 154 : 0;
    }
}
static PlatformSurface *loadPng(const TArtStyle *style, const char *name,
                               int image, int width, int height)
{
    char path[512];
    assetPath(path, sizeof(path), style, name, image);
    size_t size = 0;
    uint8 *data = zipvfs_read(path, &size);
    if (!data) {
        if (style == &styles[1] && zipvfs_exists(path))
            fatalError("Cannot read art asset %s", path);
        return NULL;
    }
    int strict = style == &styles[1];
    /* Apply the portable decoder's format contract on Windows too. */
    if (strict && (size < 33 || memcmp(data, "\211PNG\r\n\032\n", 8) != 0 ||
        memcmp(data + 8, "\0\0\0\15IHDR", 8) != 0 || data[24] != 8 ||
        (data[25] != 2 && data[25] != 4 && data[25] != 6) ||
        data[26] != 0 || data[27] != 0 || data[28] != 0)) {
        free(data);
        fatalError("Invalid art PNG %s: expected 8-bit non-interlaced RGB/RGBA/grayscale-alpha", path);
    }
    PlatformSurface *surface = platformLoadPNGFromMemory(data, size);
    free(data);
    if (!surface) {
        if (strict) fatalError("Cannot decode art PNG %s", path);
        return NULL;
    }
    int w = platformGetSurfaceWidth(surface), h = platformGetSurfaceHeight(surface);
    int registeredFootprint = strict && currentScale == 2 &&
        ((width == 280 && height == 52 && extendedIsland(name, image, w, h)) ||
         (width == 160 && height == 25 && extendedCenterFoam(name, image, w, h)) ||
         (width == 72 && height == 29 && extendedLeftFoam(name, image, w, h)) ||
         (width == 72 && height == 32 && extendedRightFoam(name, image, w, h)));
    if (selected == &styles[1] && !registeredFootprint &&
        (w != width * currentScale || h != height * currentScale)) {
        releaseLoadedSurface(surface);
        if (strict)
            fatalError("Art PNG %s is %dx%d; expected %dx%d", path, w, h,
                       width * currentScale, height * currentScale);
        else
            debugMsg("Art fallback: ignoring wrong-sized HD PNG %s (%dx%d)", path, w, h);
        return NULL;
    }
    if (image >= 0 && style->legacyColorKey) {
        uint8 *pixels = platformGetSurfacePixels(surface);
        int pitch = platformGetSurfacePitch(surface);
        if (pixels && platformGetSurfaceBytesPerPixel(surface) == 4) {
            for (int y = 0; y < h; y++) {
                uint8 *row = pixels + (size_t)y * (size_t)pitch;
                for (int x = 0; x < w; x++) {
                    uint8 *p = row + (size_t)x * 4;
                    if (p[3] == 255 && p[0] == 0xA8 && p[1] == 0 && p[2] == 0xA8)
                        p[0] = p[1] = p[2] = p[3] = 0;
                }
            }
        }
    }
    debugMsg("Art asset: %s", path);
    return surface;
}
static PlatformSurface *loadAsset(const char *name, int image, int width, int height)
{
    PlatformSurface *surface = NULL;
    if (selected == &styles[1]) {
        surface = loadPng(selected, name, image, width, height);
        if (surface) { selectedLoads++; return surface; }
    }
    if (hdScale > 1 && hdScale == currentScale) {
        surface = loadPng(&styles[0], name, image, width, height);
        if (surface) { hdLoads++; return surface; }
    }
    originalLoads++;
    return NULL;
}
PlatformSurface *artStyleLoadScreen(const char *name, int width, int height)
{
    return loadAsset(name, -1, width, height);
}
PlatformSurface *artStyleLoadSprite(const char *name, int image, int width, int height)
{
    return loadAsset(name, image, width, height);
}
void artStyleReportUsage(void)
{
    if (usageReported || !validated) return;
    usageReported = 1;
    if (selected == &styles[1])
        printf("Art assets decoded: cartoon=%lu, HD fallback=%lu, original fallback=%lu\n",
               selectedLoads, hdLoads, originalLoads);
}
