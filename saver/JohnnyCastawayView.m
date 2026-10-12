/*
 *  Johnny Castaway for macOS - the ScreenSaverView.
 *
 *  One engine per process. legacyScreenSaver creates one view per display
 *  (and System Settings creates one for its preview), while the engine is a
 *  single-instance C program full of static state, so the first view to
 *  animate starts the engine on a worker thread and every view draws the
 *  same front buffer. The last view to stop asks the engine to quit.
 *
 *  Compiled with ARC.
 *
 *  This file is part of 'Johnny Castaway for macOS' (GPL-3.0-or-later).
 */

#import <ScreenSaver/ScreenSaver.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "platform_saver.h"

static NSString * const kModuleName   = @"com.newideasunlimited.JohnnyCastaway";
static NSString * const kPrefSound    = @"SoundEnabled";   /* BOOL */
static NSString * const kPrefArtStyle = @"ArtStyle";       /* "hd" | "cartoon" */
static NSString * const kPrefScaling  = @"Scaling";        /* "extend" | "crop" | "stretch" | "fit" | "integer" */
static NSString * const kPrefDayNight = @"DayNight";       /* "auto" | "day" | "night" */

/* ── Process-wide engine state ───────────────────────────────────────────── */

static pthread_mutex_t gEngineMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gEngineCond  = PTHREAD_COND_INITIALIZER;
static BOOL            gEngineRunning = NO;
static NSInteger       gActiveViews = 0;

typedef struct {
    int   argc;
    char *argv[16];
} EngineArgs;

static void engineArgsAdd(EngineArgs *args, const char *value)
{
    if (args->argc < 15) {
        args->argv[args->argc++] = strdup(value);
        args->argv[args->argc] = NULL;
    }
}

/* Runs when the engine thread ends, whether jcEngineMain returns or the
 * engine leaves through jcExit() -> platformEmbeddedExit() -> pthread_exit(). */
static void engineThreadCleanup(void *arg)
{
    EngineArgs *args = (EngineArgs *)arg;
    for (int i = 0; i < args->argc; i++) free(args->argv[i]);
    free(args);

    pthread_mutex_lock(&gEngineMutex);
    gEngineRunning = NO;
    jcSaverClearQuit();
    pthread_cond_broadcast(&gEngineCond);
    pthread_mutex_unlock(&gEngineMutex);
}

static void *engineThreadMain(void *arg)
{
    EngineArgs *args = (EngineArgs *)arg;
    pthread_cleanup_push(engineThreadCleanup, args);
    jcEngineMain(args->argc, args->argv);
    pthread_cleanup_pop(1);
    return NULL;
}

static BOOL engineIsRunning(void)
{
    pthread_mutex_lock(&gEngineMutex);
    BOOL running = gEngineRunning;
    pthread_mutex_unlock(&gEngineMutex);
    return running;
}

static BOOL engineStart(EngineArgs *args)
{
    pthread_mutex_lock(&gEngineMutex);
    if (gEngineRunning) {
        pthread_mutex_unlock(&gEngineMutex);
        for (int i = 0; i < args->argc; i++) free(args->argv[i]);
        free(args);
        return NO;
    }
    gEngineRunning = YES;
    pthread_mutex_unlock(&gEngineMutex);

    jcSaverClearQuit();
    jcSaverClearFatal();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t thread;
    int rc = pthread_create(&thread, &attr, engineThreadMain, args);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        pthread_mutex_lock(&gEngineMutex);
        gEngineRunning = NO;
        pthread_mutex_unlock(&gEngineMutex);
        for (int i = 0; i < args->argc; i++) free(args->argv[i]);
        free(args);
        return NO;
    }
    return YES;
}

/* Ask the engine to quit and wait up to `timeout` seconds for its thread to
 * end. Waiting is bounded because this runs on the main thread. */
static void engineStopAndWait(NSTimeInterval timeout)
{
    pthread_mutex_lock(&gEngineMutex);
    if (gEngineRunning) {
        jcSaverRequestQuit();

        struct timeval now;
        gettimeofday(&now, NULL);
        double deadline = (double)now.tv_sec + (double)now.tv_usec / 1e6 + timeout;
        struct timespec ts;
        ts.tv_sec = (time_t)deadline;
        ts.tv_nsec = (long)((deadline - (double)ts.tv_sec) * 1e9);

        while (gEngineRunning) {
            if (pthread_cond_timedwait(&gEngineCond, &gEngineMutex, &ts) != 0) break;
        }
    }
    pthread_mutex_unlock(&gEngineMutex);
}

/* ── The view ────────────────────────────────────────────────────────────── */

@interface JohnnyCastawayView : ScreenSaverView
{
    uint8_t  *_frameCopy;
    size_t    _frameCopyBytes;
    int       _frameWidth, _frameHeight, _framePitch;
    uint64_t  _drawnSerial;
    NSInteger _hiddenFrames;
    BOOL      _dismissed;
}
@property (nonatomic, strong) NSWindow      *configSheet;
@property (nonatomic, strong) NSButton      *soundCheckbox;
@property (nonatomic, strong) NSPopUpButton *stylePopup;
@property (nonatomic, strong) NSPopUpButton *scalingPopup;
@property (nonatomic, strong) NSPopUpButton *dayNightPopup;
@end

@implementation JohnnyCastawayView

+ (ScreenSaverDefaults *)defaults
{
    ScreenSaverDefaults *defaults = [ScreenSaverDefaults defaultsForModuleWithName:kModuleName];
    [defaults registerDefaults:@{
        kPrefSound:    @YES,
        kPrefArtStyle: @"hd",
        kPrefScaling:  @"extend",
        kPrefDayNight: @"auto",
    }];
    return defaults;
}

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview
{
    self = [super initWithFrame:frame isPreview:isPreview];
    if (self) {
        [self setAnimationTimeInterval:1.0 / 30.0];
        _drawnSerial = (uint64_t)-1;

        /*  WHY THESE OBSERVERS EXIST. On macOS 14 and later the host process
         *  (legacyScreenSaver) is not reliably told to stop when the screen
         *  saver is dismissed: the window goes away but stopAnimation may
         *  never arrive and the process lingers, so the engine kept running
         *  and the sound effects kept playing over the desktop. The system
         *  does post these distributed notifications, so they are the
         *  trustworthy signal. */
        if (!isPreview) {
            NSDistributedNotificationCenter *center = [NSDistributedNotificationCenter defaultCenter];
            [center addObserver:self selector:@selector(screenSaverWillStop:)
                           name:@"com.apple.screensaver.willstop" object:nil];
            [center addObserver:self selector:@selector(screenSaverWillStop:)
                           name:@"com.apple.screensaver.didstop" object:nil];
        }
    }
    return self;
}

- (void)dealloc
{
    [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];
    free(_frameCopy);
}

/* ── Dismissal ─────────────────────────────────────────────────────────── */

- (void)screenSaverWillStop:(NSNotification *)notification
{
    [self shutDownForDismissal];
}

/* Silence first, then stop the engine, then leave. Exiting the host process
 * is deliberate: once the saver has been dismissed there is nothing left for
 * legacyScreenSaver to do, and on macOS 14+ it would otherwise sit there with
 * the engine thread alive. The system starts a fresh host next time. Never
 * done for the System Settings preview, which must keep its process. */
- (void)shutDownForDismissal
{
    if (_dismissed || [self isPreview]) return;
    _dismissed = YES;

    jcSaverSetMuted(1);
    gActiveViews = 0;
    engineStopAndWait(1.5);
    exit(0);
}

- (BOOL)isOpaque { return YES; }

/* ── Engine lifecycle ──────────────────────────────────────────────────── */

- (EngineArgs *)engineArgs
{
    ScreenSaverDefaults *defaults = [[self class] defaults];
    EngineArgs *args = (EngineArgs *)calloc(1, sizeof(EngineArgs));
    engineArgsAdd(args, "JohnnyCastaway");

    /* The preview in System Settings must not make noise. */
    if (![defaults boolForKey:kPrefSound] || [self isPreview])
        engineArgsAdd(args, "nosound");

    NSString *style = [defaults stringForKey:kPrefArtStyle];
    if (![style isEqualToString:@"cartoon"]) style = @"hd";
    engineArgsAdd(args, "style");
    engineArgsAdd(args, style.UTF8String);

    NSString *dayNight = [defaults stringForKey:kPrefDayNight];
    if ([dayNight isEqualToString:@"day"])   engineArgsAdd(args, "day");
    if ([dayNight isEqualToString:@"night"]) engineArgsAdd(args, "night");

    return args;
}

- (void)ensureEngine
{
    if (gActiveViews <= 0) return;
    if (engineIsRunning()) return;
    if (jcSaverQuitRequested()) return;     /* previous run still winding down */
    if (jcSaverLastFatal() != NULL) return; /* it died; show the message, do not loop */

    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *zip = [bundle pathForResource:@"scrantic_data" ofType:@"zip"];
    if (zip) {
        setenv("JC_REBORN_DATA", zip.fileSystemRepresentation, 1);
    }

    jcSaverSetMuted(0);
    engineStart([self engineArgs]);
}

- (void)startAnimation
{
    [super startAnimation];
    gActiveViews++;
    [self ensureEngine];
}

- (void)stopAnimation
{
    [super stopAnimation];
    gActiveViews--;
    if (gActiveViews <= 0) {
        gActiveViews = 0;
        jcSaverSetMuted(1);
        engineStopAndWait(2.0);
    }
}

- (void)animateOneFrame
{
    /*  Fallback for a dismissal that posted no notification: a saver window
     *  hidden for five seconds is not on anyone's screen, so go quiet and
     *  stop the engine. Deliberately not an exit(): if the window ever comes
     *  back the engine simply restarts, so a false reading costs nothing. */
    if (![self isPreview] && self.window != nil) {
        BOOL visible = ([self.window occlusionState] & NSWindowOcclusionStateVisible) != 0;
        _hiddenFrames = visible ? 0 : _hiddenFrames + 1;
        if (_hiddenFrames == 150) {
            jcSaverSetMuted(1);
            engineStopAndWait(0.5);
        }
        if (_hiddenFrames >= 150) return;
    }

    [self ensureEngine];
    if (jcSaverFrameSerial() != _drawnSerial || jcSaverLastFatal() != NULL) {
        [self setNeedsDisplay:YES];
    }
}

/* ── Drawing ───────────────────────────────────────────────────────────── */

- (BOOL)copyFrontBuffer
{
    JCSaverFrame frame;
    BOOL have = NO;
    if (jcSaverLockFrame(&frame)) {
        size_t bytes = (size_t)frame.pitch * (size_t)frame.height;
        if (bytes != _frameCopyBytes) {
            free(_frameCopy);
            _frameCopy = (uint8_t *)malloc(bytes);
            _frameCopyBytes = _frameCopy ? bytes : 0;
        }
        if (_frameCopy) {
            memcpy(_frameCopy, frame.pixels, bytes);
            _frameWidth = frame.width;
            _frameHeight = frame.height;
            _framePitch = frame.pitch;
            _drawnSerial = jcSaverFrameSerial();
            have = YES;
        }
    }
    jcSaverUnlockFrame();
    return have;
}

- (NSString *)scalingMode
{
    NSString *mode = [[[self class] defaults] stringForKey:kPrefScaling];
    if ([mode isEqualToString:@"crop"] || [mode isEqualToString:@"stretch"] ||
        [mode isEqualToString:@"fit"] || [mode isEqualToString:@"integer"])
        return mode;
    return @"extend";
}

/* The engine renders 4:3. The screen almost never is, so the frame is
 * placed according to the chosen mode:
 *   extend   fit inside, then fill the bars with mirrored copies of the
 *            frame's own edges (ocean and sky), so the picture reaches the
 *            screen edges and nothing is cut off          (default)
 *   crop     scale to cover the screen, trimming top and bottom
 *   stretch  distort to the screen's aspect
 *   fit      letterbox, black bars
 *   integer  letterbox at a whole-number pixel scale (sharpest)
 */
- (NSRect)destinationRectForFrameInBounds:(NSRect)bounds mode:(NSString *)mode
{
    if ([mode isEqualToString:@"stretch"]) return bounds;

    double scaleX = bounds.size.width / (double)_frameWidth;
    double scaleY = bounds.size.height / (double)_frameHeight;
    double scale;

    if ([mode isEqualToString:@"crop"]) {
        scale = scaleX > scaleY ? scaleX : scaleY;
    } else {
        scale = scaleX < scaleY ? scaleX : scaleY;
        if ([mode isEqualToString:@"integer"] && scale >= 1.0) {
            /* Backing pixels, not points: on a Retina display a 2x view can
             * show the 1280x960 render at an exact pixel mapping. */
            double backing = [self.window backingScaleFactor];
            if (backing <= 0) backing = 1.0;
            scale = floor(scale * backing) / backing;
            if (scale < 1.0 / backing) scale = 1.0 / backing;
        }
    }

    double w = _frameWidth * scale;
    double h = _frameHeight * scale;
    return NSMakeRect(floor(bounds.origin.x + (bounds.size.width - w) * 0.5),
                      floor(bounds.origin.y + (bounds.size.height - h) * 0.5), w, h);
}

- (void)drawFrame
{
    if (!_frameCopy || _frameWidth <= 0 || _frameHeight <= 0) return;

    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL, _frameCopy, (size_t)_framePitch * (size_t)_frameHeight, NULL);

    /* Premultiplied BGRA in memory (B=0, G=1, R=2, A=3); the alpha byte is
     * always 255 on the presented frame, so it is skipped. */
    CGImageRef image = CGImageCreate((size_t)_frameWidth, (size_t)_frameHeight,
                                     8, 32, (size_t)_framePitch, colorSpace,
                                     (CGBitmapInfo)kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little,
                                     provider, NULL, false, kCGRenderingIntentDefault);

    NSRect bounds = [self bounds];
    NSString *mode = [self scalingMode];
    NSRect dest = [self destinationRectForFrameInBounds:bounds mode:mode];
    CGRect cgDest = NSRectToCGRect(dest);

    /* The art is hard-edged pixel art: keep it crisp when enlarging, and only
     * smooth when shrinking (the System Settings preview). */
    double scale = dest.size.width / (double)_frameWidth;
    CGContextSetInterpolationQuality(context, scale >= 1.0 ? kCGInterpolationNone
                                                           : kCGInterpolationHigh);

    CGContextSaveGState(context);
    CGContextClipToRect(context, NSRectToCGRect(bounds));
    CGContextDrawImage(context, cgDest, image);

    if ([mode isEqualToString:@"extend"]) {
        /* Mirror the frame into whichever bars remain. Horizontally mirrored
         * copies sit to the left and right; vertically mirrored ones above
         * and below (portrait screens). Each copy is the same frame drawn
         * under a flipped transform, anchored so its edge meets ours. */
        if (dest.origin.x > bounds.origin.x + 0.5 ||
            NSMaxX(dest) < NSMaxX(bounds) - 0.5) {
            CGContextSaveGState(context);
            CGContextTranslateCTM(context, cgDest.origin.x, 0);
            CGContextScaleCTM(context, -1, 1);
            CGContextDrawImage(context, CGRectMake(0, cgDest.origin.y, cgDest.size.width, cgDest.size.height), image);
            CGContextRestoreGState(context);

            CGContextSaveGState(context);
            CGContextTranslateCTM(context, cgDest.origin.x + 2 * cgDest.size.width, 0);
            CGContextScaleCTM(context, -1, 1);
            CGContextDrawImage(context, CGRectMake(0, cgDest.origin.y, cgDest.size.width, cgDest.size.height), image);
            CGContextRestoreGState(context);
        }
        if (dest.origin.y > bounds.origin.y + 0.5 ||
            NSMaxY(dest) < NSMaxY(bounds) - 0.5) {
            CGContextSaveGState(context);
            CGContextTranslateCTM(context, 0, cgDest.origin.y);
            CGContextScaleCTM(context, 1, -1);
            CGContextDrawImage(context, CGRectMake(cgDest.origin.x, 0, cgDest.size.width, cgDest.size.height), image);
            CGContextRestoreGState(context);

            CGContextSaveGState(context);
            CGContextTranslateCTM(context, 0, cgDest.origin.y + 2 * cgDest.size.height);
            CGContextScaleCTM(context, 1, -1);
            CGContextDrawImage(context, CGRectMake(cgDest.origin.x, 0, cgDest.size.width, cgDest.size.height), image);
            CGContextRestoreGState(context);
        }
    }
    CGContextRestoreGState(context);

    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
}

- (void)drawMessage:(NSString *)message
{
    NSRect bounds = [self bounds];
    CGFloat size = [self isPreview] ? 9.0 : 18.0;
    NSDictionary *attributes = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor whiteColor],
    };
    NSRect box = NSInsetRect(bounds, 20, 20);
    [message drawInRect:box withAttributes:attributes];
}

- (void)drawRect:(NSRect)dirtyRect
{
    [[NSColor blackColor] setFill];
    NSRectFill([self bounds]);

    if ([self copyFrontBuffer] || _frameCopy) {
        [self drawFrame];
    }

    const char *fatal = jcSaverLastFatal();
    if (fatal) {
        NSString *text = [NSString stringWithFormat:
            @"Johnny Castaway could not start.\n\n%s\n\n"
             "The game data (scrantic_data.zip) must be inside the screen saver bundle: "
             "rebuild with 'make install'.", fatal];
        [self drawMessage:text];
    }
}

/* ── Configure sheet ───────────────────────────────────────────────────── */

- (BOOL)hasConfigureSheet { return YES; }

- (NSPopUpButton *)popupWithTitles:(NSArray<NSString *> *)titles y:(CGFloat)y
{
    NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(150, y, 290, 26) pullsDown:NO];
    [popup addItemsWithTitles:titles];
    [self.configSheet.contentView addSubview:popup];
    return popup;
}

- (void)addLabel:(NSString *)text y:(CGFloat)y
{
    NSTextField *label = [NSTextField labelWithString:text];
    [label setFrame:NSMakeRect(20, y + 4, 125, 20)];
    [label setAlignment:NSTextAlignmentRight];
    [self.configSheet.contentView addSubview:label];
}

- (NSWindow *)configureSheet
{
    if (self.configSheet) {
        [self loadConfigSheetValues];
        return self.configSheet;
    }

    NSRect frame = NSMakeRect(0, 0, 460, 236);
    self.configSheet = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    [self.configSheet setTitle:@"Johnny Castaway"];

    NSTextField *heading = [NSTextField labelWithString:@"Johnny Castaway"];
    [heading setFont:[NSFont boldSystemFontOfSize:15]];
    [heading setFrame:NSMakeRect(20, 198, 420, 22)];
    [self.configSheet.contentView addSubview:heading];

    [self addLabel:@"Artwork:" y:156];
    self.stylePopup = [self popupWithTitles:@[@"HD (upscaled original)", @"Cartoon (preview)"] y:156];

    [self addLabel:@"Time of day:" y:122];
    self.dayNightPopup = [self popupWithTitles:@[@"Follow the clock", @"Always day", @"Always night"] y:122];

    [self addLabel:@"Scaling:" y:88];
    self.scalingPopup = [self popupWithTitles:@[@"Fill screen, extend ocean and sky",
                                                @"Fill screen, crop top and bottom",
                                                @"Stretch to fill",
                                                @"Fit with black bars",
                                                @"Integer scale (sharpest, bars)"] y:88];

    self.soundCheckbox = [NSButton checkboxWithTitle:@"Play sound effects" target:nil action:NULL];
    [self.soundCheckbox setFrame:NSMakeRect(150, 56, 290, 20)];
    [self.configSheet.contentView addSubview:self.soundCheckbox];

    NSButton *cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(configCancel:)];
    [cancel setFrame:NSMakeRect(270, 14, 84, 32)];
    [cancel setKeyEquivalent:@"\033"];
    [self.configSheet.contentView addSubview:cancel];

    NSButton *ok = [NSButton buttonWithTitle:@"OK" target:self action:@selector(configOK:)];
    [ok setFrame:NSMakeRect(358, 14, 84, 32)];
    [ok setKeyEquivalent:@"\r"];
    [self.configSheet.contentView addSubview:ok];

    [self loadConfigSheetValues];
    return self.configSheet;
}

/* Same order as the Scaling popup. */
- (NSArray<NSString *> *)scalingModes
{
    return @[@"extend", @"crop", @"stretch", @"fit", @"integer"];
}

- (void)loadConfigSheetValues
{
    ScreenSaverDefaults *defaults = [[self class] defaults];
    [self.stylePopup selectItemAtIndex:
        [[defaults stringForKey:kPrefArtStyle] isEqualToString:@"cartoon"] ? 1 : 0];
    NSString *dayNight = [defaults stringForKey:kPrefDayNight];
    [self.dayNightPopup selectItemAtIndex:
        [dayNight isEqualToString:@"day"] ? 1 : [dayNight isEqualToString:@"night"] ? 2 : 0];
    NSUInteger scalingIndex = [[self scalingModes] indexOfObject:[self scalingMode]];
    [self.scalingPopup selectItemAtIndex:(scalingIndex == NSNotFound ? 0 : (NSInteger)scalingIndex)];
    [self.soundCheckbox setState:[defaults boolForKey:kPrefSound] ? NSControlStateValueOn
                                                                  : NSControlStateValueOff];
}

- (void)dismissConfigSheet:(NSModalResponse)response
{
    NSWindow *sheet = self.configSheet;
    NSWindow *parent = [sheet sheetParent];
    if (parent) {
        [parent endSheet:sheet returnCode:response];
    } else {
        [NSApp endSheet:sheet returnCode:response];
        [sheet orderOut:nil];
    }
}

- (void)configCancel:(id)sender
{
    [self dismissConfigSheet:NSModalResponseCancel];
}

- (void)configOK:(id)sender
{
    ScreenSaverDefaults *defaults = [[self class] defaults];
    [defaults setObject:([self.stylePopup indexOfSelectedItem] == 1 ? @"cartoon" : @"hd")
                 forKey:kPrefArtStyle];
    NSArray *dayNight = @[@"auto", @"day", @"night"];
    NSInteger dn = [self.dayNightPopup indexOfSelectedItem];
    [defaults setObject:dayNight[(dn >= 0 && dn < 3) ? dn : 0] forKey:kPrefDayNight];
    NSInteger sc = [self.scalingPopup indexOfSelectedItem];
    NSArray *modes = [self scalingModes];
    [defaults setObject:modes[(sc >= 0 && sc < (NSInteger)modes.count) ? sc : 0] forKey:kPrefScaling];
    [defaults setBool:([self.soundCheckbox state] == NSControlStateValueOn) forKey:kPrefSound];
    [defaults synchronize];

    [self dismissConfigSheet:NSModalResponseOK];

    /* Restart the engine with the new options; the next animateOneFrame
     * starts it again if any view is animating. */
    engineStopAndWait(2.0);
    jcSaverClearFatal();
    [self setNeedsDisplay:YES];
}

@end
