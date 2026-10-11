# Johnny Castaway for macOS
#
#   make            build build/JohnnyCastaway.saver          (macOS only)
#   make install    copy it into ~/Library/Screen Savers       (macOS only)
#   make cli        build build/jc_reborn, a windowed test app (macOS only)
#   make headless   build build/jc_headless, no display needed (any OS)
#   make data       download the game data archive (scrantic_data.zip)
#   make clean
#
# The game data is not in this repository; `make data` fetches it. See README.

ifeq ($(origin CC),default)
CC       := clang
endif
VERSION  := 1.0.1
ENGINE   := engine
BUILD    := build
DATA     := data/scrantic_data.zip

UNAME_S  := $(shell uname -s)

INCLUDES := -I$(ENGINE)/src/engine -I$(ENGINE)/src/data -I$(ENGINE)/platform -I$(ENGINE)/third_party/miniz
WARN     := -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-unused-function
CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 $(WARN) $(INCLUDES) -DJC_VERSION=\"$(VERSION)\"

ENGINE_SRC := $(wildcard $(ENGINE)/src/engine/*.c) \
              $(ENGINE)/platform/png_loader.c \
              $(ENGINE)/platform/png_decoder.c \
              $(ENGINE)/platform/zipvfs.c \
              $(ENGINE)/third_party/miniz/miniz.c \
              $(ENGINE)/third_party/miniz/miniz_tdef.c \
              $(ENGINE)/third_party/miniz/miniz_tinfl.c \
              $(ENGINE)/third_party/miniz/miniz_zip.c

# ── macOS screen saver ─────────────────────────────────────────────────────────
SAVER_NAME  := JohnnyCastaway
SAVER       := $(BUILD)/$(SAVER_NAME).saver
SAVER_BIN   := $(SAVER)/Contents/MacOS/$(SAVER_NAME)
SAVER_SRC   := saver/JohnnyCastawayView.m saver/platform_saver.c $(ENGINE)/platform/platform_soft.c
ARCHS       ?= -arch arm64 -arch x86_64
MACOS_MIN   ?= -mmacosx-version-min=11.0
MAC_CFLAGS  := $(ARCHS) $(MACOS_MIN) -DPLATFORM_SAVER -DJC_EMBEDDED -DJC_MAIN=jcEngineMain
SAVER_FRAMEWORKS := -framework ScreenSaver -framework Cocoa -framework AudioToolbox -framework QuartzCore
SAVER_OBJDIR := $(BUILD)/obj-saver
SAVER_OBJS   := $(patsubst %.c,$(SAVER_OBJDIR)/%.o,$(filter %.c,$(ENGINE_SRC) $(SAVER_SRC))) \
                $(patsubst %.m,$(SAVER_OBJDIR)/%.o,$(filter %.m,$(SAVER_SRC)))

INSTALL_DIR := $(HOME)/Library/Screen Savers

ifeq ($(UNAME_S),Darwin)
.PHONY: all
all: saver
else
.PHONY: all
all: headless
endif

.PHONY: saver
saver: check-macos $(SAVER_BIN) $(SAVER)/Contents/Info.plist $(SAVER)/Contents/Resources/scrantic_data.zip $(SAVER)/Contents/Resources/thumbnail.png $(SAVER)/Contents/Resources/thumbnail@2x.png
	codesign --force --sign - --timestamp=none "$(SAVER)"
	@echo
	@echo "Built $(SAVER)"
	@echo "Run 'make install' to copy it to '$(INSTALL_DIR)'."

$(SAVER_OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MAC_CFLAGS) -c -o $@ $<

$(SAVER_OBJDIR)/%.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MAC_CFLAGS) -fobjc-arc -x objective-c -c -o $@ $<

$(SAVER_BIN): $(SAVER_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(ARCHS) $(MACOS_MIN) -bundle -o $@ $(SAVER_OBJS) $(SAVER_FRAMEWORKS)

$(SAVER)/Contents/Info.plist: saver/Info.plist
	@mkdir -p $(dir $@)
	sed -e 's/@VERSION@/$(VERSION)/g' $< > $@

$(SAVER)/Contents/Resources/scrantic_data.zip: $(DATA)
	@mkdir -p $(dir $@)
	cp $< $@

$(SAVER)/Contents/Resources/thumbnail.png: saver/thumbnail.png
	@mkdir -p $(dir $@)
	cp $< $@

$(SAVER)/Contents/Resources/thumbnail@2x.png: saver/thumbnail@2x.png
	@mkdir -p $(dir $@)
	cp $< $@

.PHONY: install
install: saver
	@mkdir -p "$(INSTALL_DIR)"
	rm -rf "$(INSTALL_DIR)/$(SAVER_NAME).saver"
	cp -R "$(SAVER)" "$(INSTALL_DIR)/"
	@# System Settings caches loaded savers; make it pick up the new build.
	-killall "legacyScreenSaver" 2>/dev/null; true
	-killall "legacyScreenSaver-x86_64" 2>/dev/null; true
	@echo
	@echo "Installed to '$(INSTALL_DIR)/$(SAVER_NAME).saver'."
	@echo "Open System Settings > Screen Saver and pick 'Johnny Castaway'."
	@echo "(If it was already open, quit System Settings first.)"

.PHONY: uninstall
uninstall:
	rm -rf "$(INSTALL_DIR)/$(SAVER_NAME).saver"

# ── macOS windowed test app (upstream Cocoa backend, unmodified) ───────────────
CLI_OBJDIR := $(BUILD)/obj-cli
.PHONY: cli
cli: check-macos $(BUILD)/jc_reborn
$(BUILD)/jc_reborn: $(ENGINE_SRC) $(ENGINE)/platform/platform_macos.m $(DATA)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(MACOS_MIN) -DPLATFORM_MACOS -o $@ $(ENGINE_SRC) \
	    -x objective-c $(ENGINE)/platform/platform_macos.m \
	    -framework Cocoa -framework AudioToolbox
	cp $(DATA) $(BUILD)/scrantic_data.zip
	@echo "Try: $(BUILD)/jc_reborn window hotkeys"

# ── headless build, for tests and frame dumps (any OS) ─────────────────────────
.PHONY: headless
headless: $(BUILD)/jc_headless
$(BUILD)/jc_headless: $(ENGINE_SRC) tools/platform_headless.c $(ENGINE)/platform/platform_soft.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DPLATFORM_HEADLESS -o $@ $(ENGINE_SRC) tools/platform_headless.c $(ENGINE)/platform/platform_soft.c -lm

# ── data ───────────────────────────────────────────────────────────────────────
.PHONY: data
data: $(DATA)
$(DATA):
	sh scripts/fetch-data.sh "$(DATA)"

.PHONY: check-macos
check-macos:
ifneq ($(UNAME_S),Darwin)
	$(error This target needs macOS with the Xcode Command Line Tools (xcode-select --install))
endif

.PHONY: clean
clean:
	rm -rf $(BUILD)
