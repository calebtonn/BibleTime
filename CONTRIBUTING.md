# Contributing to BibleTime

Thanks for helping. This file covers the development loop; the README covers what the project is.

## Requirements

- macOS 12 or later. Development has been done on macOS 26.
- Xcode **Command Line Tools** (`xcode-select --install`). Full Xcode is optional.
- Python 3 (the system one is fine) only if you regenerate the verse data.
- `xcodegen` (`brew install xcodegen`) only if you want an Xcode project.

## Build and run

```bash
make            # build/BibleTime.saver, universal (arm64 + x86_64), ad-hoc signed
make test       # core unit tests (no XCTest needed)
make render     # screenshots of every layout into build/renders/
make install    # copy into ~/Library/Screen Savers and restart the preview processes
```

Everything is compiled with `swiftc` directly; see the `Makefile`. There is no Xcode-only step.

### The development loop

System Settings caches screen saver bundles and gives no feedback when one fails to load, so do not iterate
there. Use the host instead:

```bash
make render
open build/renders/clock-paper.png
```

`Tools/SaverHost.swift` loads the bundle exactly the way `legacyScreenSaver` does (`Bundle.load()` →
`principalClass` → `init(frame:isPreview:)` → `startAnimation()`), pumps the run loop, and writes a PNG. Two
environment variables make renders reproducible:

| Variable | Example | Effect |
| --- | --- | --- |
| `BIBLETIME_TIME` | `15:16` | Pin the clock to today at that time. |
| `BIBLETIME_PREFS` | `{"layout":"book","theme":"night"}` | Override stored settings (keys as in `Preferences.swift`). |
| `BIBLETIME_REF` | `EST 8:9` | Force a verse by USFM book code, whatever the time. |

`Tools/build_from_biblia.py` builds a translation file from the Biblia API for personal import; it reuses the flag
heuristics from `build_verses.py` and has no tests that hit the network (its parser is exercised by hand with `--only`).

`SaverHost` takes `--out`, `--width`, `--height`, `--seconds` (how long to pump the run loop) and `--preview`
(instantiate the view the way System Settings' thumbnail does).

Example:

```bash
BIBLETIME_TIME=11:35 BIBLETIME_PREFS='{"layout":"centered","textScale":1.4}' build/SaverHost build/BibleTime.saver --out out.png --width 2560 --height 1440
```

### Testing inside System Settings

`make install` copies the bundle and kills `legacyScreenSaver`, `WallpaperAgent` and System Settings so the
new build is picked up. If the old build still shows (a long-standing macOS caching bug), select a different
screen saver, preview it, come back, or log out and in. Recent macOS versions have known bugs with
third-party screen savers: the preview instance can be created with a zero-size frame, instances are not
always stopped, and a secondary display can go black. The view guards against the first; the others are
Apple's to fix.

## Project layout

```
Sources/BibleTimeCore/   Foundation-only logic: canon, translations, selection, preferences
Sources/BibleTime/       AppKit: the ScreenSaverView, renderer, theme, options sheet
Resources/               books.json, curated.json, hour_verses.json, Translations/*.json
Tools/                   build_verses.py (data pipeline), SaverHost.swift (dev host)
Tests/CoreTests/         plain-Swift test executable (run by `make test` or `swift run CoreTests Resources`)
```

The core must not import AppKit so it stays testable and portable. The Makefile compiles both folders into
one module named `BibleTime`; `Package.swift` exposes only the core.

## Verse data

`Resources/Translations/*.json` is generated from eBible.org's USFX files by `Tools/build_verses.py`.
Regenerate with `make data` (downloads the sources) and check the result with `make check-data`. Please do
not hand-edit the generated files. Changes to the curated lists go in `Tools/build_verses.py` or the
`curated.json` / `hour_verses.json` inputs, and the tests in `Tests/CoreTests` must keep passing.

## Imported translations

`TranslationImporter` (core) validates a file and copies it to the first of `VerseClock.defaultImportDirectories()`.
Inside the screen saver sandbox that is the legacyScreenSaver container
(`~/Library/Containers/com.apple.ScreenSaver.Engine.legacyScreenSaver/Data/Library/Application Support/BibleTime/Translations/`);
the saver also scans `~/Library/Application Support/BibleTime/Translations/` in the real home directory, which the
sandbox can read, so a file can be placed there by hand. The Options sheet runs inside that same sandboxed
process, which is why the open panel works and why the copy must go into the container. `make render` also
renders the Options sheet to `build/renders/options.png` through the host's `--sheet` flag.

## Style

- Swift 5 language mode, warnings are errors.
- Keep per-instance work small: System Settings can create several view instances at once.
- Everything the saver draws must scale with `bounds`; never assume a screen size.
- No network access, no file access outside the bundle.

## Pull requests

Run `make test` and `make render`, look at the renders, and include a screenshot when you change anything
visual.
