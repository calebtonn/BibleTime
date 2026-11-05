# BibleTime — a macOS screen saver that tells time through scripture.
#
# Builds with the Swift compiler from Xcode Command Line Tools alone (no Xcode.app needed).
#
#   make            build build/BibleTime.saver (universal, ad-hoc signed)
#   make test       run the core unit tests
#   make crosscheck compare the Windows C++ core with the Swift core (identical verse choices)
#   make render     render screenshots of every layout into build/renders/
#   make install    copy the saver to ~/Library/Screen Savers and restart the preview processes
#   make data       regenerate Resources/ from the eBible.org source files (downloads ~15 MB)
#   make xcodeproj  generate BibleTime.xcodeproj with XcodeGen (brew install xcodegen)
#   make dist       zip the bundle for a GitHub release: build/BibleTime-macOS.zip
#
# The Windows version is built with CMake instead; see Windows/CMakeLists.txt and CONTRIBUTING.md.

NAME      := BibleTime
VERSION   := $(shell cat VERSION)
ifeq ($(origin BUILD), undefined)
BUILD     := $(shell git rev-list --count HEAD 2>/dev/null || date +%Y%m%d%H%M)
endif
MIN_MACOS := 12.0
ARCHS     ?= arm64 x86_64
SDK       := $(shell xcrun --show-sdk-path)
SWIFTC    ?= swiftc
PYTHON    ?= python3
# "-" signs ad hoc (local builds, CI without secrets). Set to a "Developer ID Application: ..." identity to sign for
# distribution: that adds the hardened runtime and a secure timestamp, which notarization requires.
SIGN_IDENTITY ?= -
ifeq ($(SIGN_IDENTITY),-)
CODESIGN_FLAGS :=
else
CODESIGN_FLAGS := --options runtime --timestamp
endif

BUILD_DIR := build
BUNDLE    := $(BUILD_DIR)/$(NAME).saver
HOST      := $(BUILD_DIR)/SaverHost
TESTS     := $(BUILD_DIR)/CoreTests
INSTALL_DIR := $(HOME)/Library/Screen Savers

CORE_SRC  := $(wildcard Sources/BibleTimeCore/*.swift)
SAVER_SRC := $(wildcard Sources/BibleTime/*.swift)
RESOURCES := $(shell find Resources -type f 2>/dev/null)
PLIST     := Sources/BibleTime/Info.plist

SWIFT_FLAGS := -O -swift-version 5 -sdk "$(SDK)" -warnings-as-errors
SAVER_FLAGS := $(SWIFT_FLAGS) -parse-as-library -module-name $(NAME) -framework ScreenSaver -framework AppKit
ARCH_BINS   := $(foreach a,$(ARCHS),$(BUILD_DIR)/$(NAME)-$(a))

.PHONY: all saver test render install uninstall data xcodeproj dist clean host check-data crosscheck

all: saver
saver: $(BUNDLE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(NAME)-%: $(CORE_SRC) $(SAVER_SRC) Makefile | $(BUILD_DIR)
	$(SWIFTC) $(SAVER_FLAGS) -target $*-apple-macosx$(MIN_MACOS) -emit-library -Xlinker -bundle -o $@ $(CORE_SRC) $(SAVER_SRC)

$(BUNDLE): $(ARCH_BINS) $(PLIST) $(RESOURCES) VERSION Makefile
	rm -rf $@
	mkdir -p $@/Contents/MacOS $@/Contents/Resources
	lipo -create $(ARCH_BINS) -output $@/Contents/MacOS/$(NAME)
	sed -e 's/$$(MARKETING_VERSION)/$(VERSION)/' -e 's/$$(CURRENT_PROJECT_VERSION)/$(BUILD)/' $(PLIST) > $@/Contents/Info.plist
	printf 'BNDL????' > $@/Contents/PkgInfo
	cp -R Resources/ $@/Contents/Resources/
	codesign --force --deep $(CODESIGN_FLAGS) --sign "$(SIGN_IDENTITY)" $@
	@echo "built $@ ($(VERSION) build $(BUILD))"

# --- Development loop --------------------------------------------------------

host: $(HOST)
$(HOST): Tools/SaverHost.swift Makefile | $(BUILD_DIR)
	$(SWIFTC) $(SWIFT_FLAGS) -framework AppKit -framework ScreenSaver -o $@ $<

# Render each layout and theme at a fixed time so screenshots are reproducible.
RENDER_DIR := $(BUILD_DIR)/renders
render: $(BUNDLE) $(HOST)
	mkdir -p $(RENDER_DIR)
	BIBLETIME_TIME=15:16 BIBLETIME_PREFS='{"layout":"clock","theme":"paper"}'    $(HOST) $(BUNDLE) --out $(RENDER_DIR)/clock-paper.png
	BIBLETIME_TIME=21:16 BIBLETIME_PREFS='{"layout":"clock","theme":"night"}'    $(HOST) $(BUNDLE) --out $(RENDER_DIR)/clock-night.png
	BIBLETIME_TIME=15:16 BIBLETIME_PREFS='{"layout":"centered","theme":"paper"}' $(HOST) $(BUNDLE) --out $(RENDER_DIR)/centered-paper.png
	BIBLETIME_TIME=21:16 BIBLETIME_PREFS='{"layout":"book","theme":"night"}'     $(HOST) $(BUNDLE) --out $(RENDER_DIR)/book-night.png
	BIBLETIME_TIME=08:09 BIBLETIME_REF="EST 8:9" BIBLETIME_PREFS='{"layout":"clock","theme":"paper"}' $(HOST) $(BUNDLE) --out $(RENDER_DIR)/longest-verse.png --width 1920 --height 1080
	BIBLETIME_TIME=08:38 BIBLETIME_REF="ROM 8:38" BIBLETIME_PREFS='{"layout":"book","theme":"paper","typeface":"charter"}' $(HOST) $(BUNDLE) --out $(RENDER_DIR)/extended-fragment.png
	BIBLETIME_TIME=20:12 BIBLETIME_REF="EST 8:9" BIBLETIME_PREFS='{"layout":"centered","theme":"night","textScale":2.0}' $(HOST) $(BUNDLE) --out $(RENDER_DIR)/portrait-large.png --width 1080 --height 1920
	BIBLETIME_TIME=15:16 BIBLETIME_DISPLAY=1 BIBLETIME_PREFS='{"displayMode":"differentVerses"}' $(HOST) $(BUNDLE) --out $(RENDER_DIR)/second-display-different.png
	BIBLETIME_TIME=15:16 BIBLETIME_DISPLAY=1 BIBLETIME_PREFS='{"displayMode":"mainOnly"}' $(HOST) $(BUNDLE) --out $(RENDER_DIR)/second-display-dark.png
	BIBLETIME_TIME=15:16 BIBLETIME_PREFS='{"layout":"clock","theme":"paper"}'    $(HOST) $(BUNDLE) --out $(RENDER_DIR)/preview.png --width 296 --height 184 --preview --sheet $(RENDER_DIR)/options.png
	@echo "renders in $(RENDER_DIR)/"

test: $(TESTS)
	$(TESTS) Resources

$(TESTS): $(CORE_SRC) Tests/CoreTests/main.swift Makefile | $(BUILD_DIR)
	$(SWIFTC) $(SWIFT_FLAGS) -module-name CoreTests -o $@ $(CORE_SRC) Tests/CoreTests/main.swift

# The Windows port's C++ core (Windows/src/core) must pick exactly the verses this Swift core picks. Build both
# cross-check programs and the C++ core tests with the host compilers, run them, and diff the outputs.
CLANGXX       ?= clang++
CPP_CORE_SRC  := $(wildcard Windows/src/core/*.cpp)
CPP_FLAGS     := -std=c++17 -O2 -Wall -Wextra -Werror -IWindows/src/core
CROSSCHECK    := $(BUILD_DIR)/crosscheck
crosscheck: | $(BUILD_DIR)
	mkdir -p $(CROSSCHECK)
	$(SWIFTC) -O -swift-version 5 -sdk "$(SDK)" -module-name CrossCheck -o $(BUILD_DIR)/crosscheck-swift $(CORE_SRC) Tests/CrossCheck/main.swift
	$(CLANGXX) $(CPP_FLAGS) $(CPP_CORE_SRC) Windows/tests/CrossCheck.cpp -o $(BUILD_DIR)/crosscheck-cpp
	$(CLANGXX) $(CPP_FLAGS) $(CPP_CORE_SRC) Windows/tests/CoreTests.cpp -o $(BUILD_DIR)/core-tests-cpp
	$(BUILD_DIR)/core-tests-cpp Resources
	$(BUILD_DIR)/crosscheck-swift Resources > $(CROSSCHECK)/swift.txt
	$(BUILD_DIR)/crosscheck-cpp Resources > $(CROSSCHECK)/cpp.txt
	@if cmp -s $(CROSSCHECK)/swift.txt $(CROSSCHECK)/cpp.txt; then \
	    echo "crosscheck: Swift and C++ agree ($$(wc -l < $(CROSSCHECK)/swift.txt | tr -d ' ') lines)"; \
	else \
	    echo "crosscheck: Swift and C++ DIFFER (< swift, > cpp):"; \
	    diff $(CROSSCHECK)/swift.txt $(CROSSCHECK)/cpp.txt | head -50; \
	    exit 1; \
	fi

install: $(BUNDLE)
	mkdir -p "$(INSTALL_DIR)"
	rm -rf "$(INSTALL_DIR)/$(NAME).saver"
	cp -R $(BUNDLE) "$(INSTALL_DIR)/"
	-killall legacyScreenSaver WallpaperAgent "System Settings" 2>/dev/null; true
	@echo "installed to $(INSTALL_DIR)/$(NAME).saver — open System Settings > Wallpaper > Screen Saver"

uninstall:
	rm -rf "$(INSTALL_DIR)/$(NAME).saver"

# --- Data --------------------------------------------------------------------

DATA_DIR := Tools/data
data:
	$(PYTHON) -I Tools/build_verses.py all --data-dir $(DATA_DIR) --download \
	    --curated Tools/lists/curated_verses.json --hours Tools/lists/hour_verses.json --out-dir Resources
	$(PYTHON) -I Tools/build_verses.py verify --resources Resources --curated Tools/lists/curated_verses.json --hours Tools/lists/hour_verses.json

check-data:
	$(PYTHON) -I Tools/build_verses.py verify --resources Resources --curated Tools/lists/curated_verses.json --hours Tools/lists/hour_verses.json

# --- Packaging ---------------------------------------------------------------

xcodeproj: project.yml
	VERSION=$(VERSION) BUILD=$(BUILD) xcodegen generate

# The zip name carries no version so the README can link to releases/latest/download/BibleTime-macOS.zip.
DIST_ZIP := $(BUILD_DIR)/$(NAME)-macOS.zip
dist: $(BUNDLE)
	rm -f $(DIST_ZIP)
	cd $(BUILD_DIR) && ditto -c -k --norsrc --keepParent $(NAME).saver $(notdir $(DIST_ZIP))
	@echo "wrote $(DIST_ZIP) ($(VERSION) build $(BUILD))"

clean:
	rm -rf $(BUILD_DIR)
