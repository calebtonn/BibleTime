# Contributing to BibleTime

Thanks for helping. This file covers the development loop; the README covers what the project is.

BibleTime has two front ends that share one design: the macOS screen saver in Swift (`Sources/`) and the
Windows screen saver in C++ (`Windows/`). The first sections below are about the Mac; see [Windows](#windows) for
the other half and [Keeping the two cores identical](#keeping-the-two-cores-identical) for the rule that binds them.

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
| `BIBLETIME_DISPLAY` | `1` | Pretend the view is on the second display (0 is the main display). |

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
Tests/CrossCheck/        Swift half of the Swift/C++ cross-check (`make crosscheck`)
Windows/                 the Windows screen saver (see below)
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

## Windows

### Layout

```
Windows/CMakeLists.txt   targets: bibletime_core (static lib), core_tests, crosscheck, and BibleTime.scr on Windows
Windows/src/core/        portable C++17 port of Sources/BibleTimeCore (no Windows headers): Core.h is the API
Windows/src/app/         Win32 + Direct2D + DirectWrite screen saver, Options dialog, /render, /setactive (App.cmake)
Windows/res/             icon, manifest, resource script (the verse JSON is embedded as RCDATA)
Windows/tests/           CoreTests.cpp (mirrors Tests/CoreTests) and CrossCheck.cpp (mirrors Tests/CrossCheck)
Windows/installer/       Inno Setup script for BibleTime-Windows-Setup.exe, "How to install.txt" for the portable zip
Windows/cmake/           mingw-w64 cross-compilation toolchain
```

`BibleTime.scr` is a single self-contained executable: all verse data is compiled in, the C runtime is linked
statically, and it uses only DLLs that ship with Windows 10 and 11.

### Build

With Visual Studio 2022 (Desktop development with C++), from a Developer Command Prompt or any shell with CMake:

```bat
cmake -S Windows -B build\windows -A x64
cmake --build build\windows --config Release
build\windows\bin\core_tests.exe Resources
```

Or cross-compile on Linux or macOS with mingw-w64 (`apt install mingw-w64`, `brew install mingw-w64`):

```bash
cmake -S Windows -B build/windows-mingw -DCMAKE_TOOLCHAIN_FILE="$PWD/Windows/cmake/mingw-w64.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-mingw
```

The core and its tests build on any OS with a C++17 compiler:
`cmake -S Windows -B build/core && cmake --build build/core && build/core/bin/core_tests Resources`.

Warnings are errors (`/W4 /WX` on MSVC, `-Wall -Wextra -Werror` elsewhere); keep both clean.

### The development loop

Like System Settings on the Mac, Screen Saver Settings is a poor place to iterate. `BibleTime.scr` has two
developer switches that render offscreen and exit, and they honour the same `BIBLETIME_TIME`, `BIBLETIME_PREFS`,
`BIBLETIME_REF` and `BIBLETIME_DISPLAY` variables as `SaverHost`:

```bat
set BIBLETIME_TIME=15:16
set BIBLETIME_PREFS={"layout":"book","theme":"night"}
build\windows\bin\BibleTime.scr /render out.png --width 1920 --height 1080
build\windows\bin\BibleTime.scr /render preview.png --width 296 --height 184 --preview
build\windows\bin\BibleTime.scr /render-options options.png
```

`/render` also takes `--scale S` to simulate a high-DPI display. The other switches are the standard screen saver
ones plus two for the installer:

| Switch | Effect |
| --- | --- |
| `/s` | Run full screen on every monitor. |
| `/p <hwnd>` | Draw the preview inside the Screen Saver Settings thumbnail. |
| `/c` or none | Show the Options dialog. |
| `/setactive` | Make BibleTime the current user's screen saver (the installer's "Use BibleTime as my screen saver"). |
| `/unsetactive` | Undo that if BibleTime is still the chosen saver (run by the uninstaller). |

Right-click the built `BibleTime.scr` and choose **Install** to try it in Screen Saver Settings without the
installer. Under Wine (`WINEDEBUG=-all wine build/windows-mingw/bin/BibleTime.scr /render out.png`) the renders
work but use Wine's fonts, so check typography on real Windows.

Settings live in `%APPDATA%\BibleTime\settings.json` with the same keys and values as `Preferences.swift`;
imported translations in `%APPDATA%\BibleTime\Translations\`.

### Installer and portable zip

CI builds both (see `.github/workflows/build.yml`). Locally, after a Release build in `build\windows`:

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Windows\installer\BibleTime.iss
```

writes `build\windows\installer\BibleTime-Windows-Setup.exe`. The script reads the version from `VERSION`;
`/DAppVersion=…`, `/DSourceScr=…` and `/O…` override the version, the input `.scr` and the output folder. It
installs `BibleTime.scr` into System32 (where Screen Saver Settings looks), so it needs administrator rights;
the "Use BibleTime as my screen saver" step runs as the user who started Setup. Never change the `AppId` GUID:
it is how an upgrade finds the previous install. The portable zip is just `BibleTime.scr` plus
`Windows/installer/How to install.txt` (keep that file plain, friendly and in CRLF line endings).

## Keeping the two cores identical

`Windows/src/core` is a line-for-line port of `Sources/BibleTimeCore`: every Mac and every PC must show the same
verse at the same minute. The rule is simple: **change both**. Any change to verse selection, preferences,
canon or translation loading goes into the Swift core and the C++ core in the same pull request, with the tests
in `Tests/CoreTests` and `Windows/tests/CoreTests.cpp` kept in step, and `Tests/CrossCheck/main.swift` and
`Windows/tests/CrossCheck.cpp` printing the same sections.

`make crosscheck` (on a Mac) builds the Swift and C++ cross-check programs, runs both over every minute and
translation, and fails on any difference. CI runs it on macOS with clang, and also compares the Swift output with
the MSVC build's output from the Windows job.

## Releases

1. Bump `VERSION` (it feeds the Mac `Info.plist`, the Windows version resource and the installer).
2. Merge to `main` and push a tag `v<VERSION>`, e.g. `git tag v1.1.0 && git push origin v1.1.0`.
3. The workflow builds and tests both platforms, checks the tag matches `VERSION`, and publishes one release with
   `BibleTime-macOS.zip`, `BibleTime-Windows-Setup.exe` and `BibleTime-Windows-Portable.zip`. The file names carry
   no version so the README's `releases/latest/download/…` links keep working; do not rename them.

## Signing releases

The workflow signs the downloads when the credentials below are configured, and only for release tags and manual
runs (**Actions › Build › Run workflow**, which is also how to test the setup without releasing). Without them
everything still builds, unsigned on Windows and ad-hoc signed on the Mac. Each platform switches on independently.
Add secrets and variables in **Settings › Secrets and variables › Actions**.

### Windows: Azure Artifact Signing (about $10/month)

1. In the Azure portal create an **Artifact Signing account**, complete **identity validation** (individuals: US or
   Canada, with an Azure billing account of type Individual whose name and address match your ID) and create a
   **Public Trust certificate profile**.
2. Create an app registration (**Microsoft Entra ID › App registrations**), add a client secret, and give it the
   **Trusted Signing Certificate Profile Signer** role on the signing account.
3. Secrets: `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`.
4. Variables: `ARTIFACT_SIGNING_ENDPOINT` (the account URI, e.g. `https://eus.codesigning.azure.net/`),
   `ARTIFACT_SIGNING_ACCOUNT` (account name), `ARTIFACT_SIGNING_PROFILE` (certificate profile name).

`BibleTime.scr` is signed before it goes into the installer and the portable zip, then the installer is signed;
both signatures are checked. A new publisher still sees a SmartScreen warning until downloads build reputation (a
few weeks), but it names you as the publisher, and Smart App Control no longer blocks it.

### Mac: Developer ID and notarization ($99/year Apple Developer Program)

1. In your Apple Developer account create a **Developer ID Application** certificate, install it in Keychain Access,
   and export it with its private key as a `.p12` file with a password.
2. In App Store Connect › **Users and Access › Integrations**, create a Team API key (Developer access is enough) and
   download the `.p8` file.
3. Secrets: `MACOS_CERT_P12` (`base64 -i cert.p12 | pbcopy`), `MACOS_CERT_PASSWORD`, `APPLE_API_KEY` (the whole
   `.p8` file contents), `APPLE_API_KEY_ID`, `APPLE_API_ISSUER` (the issuer ID shown above the keys).

The bundle is then signed with the hardened runtime and a timestamp (`make SIGN_IDENTITY="Developer ID Application:
…"` does the same locally), notarized with `notarytool`, and the ticket is stapled before zipping.

Once a signed release is out, simplify the README's install steps: drop the Gatekeeper "Open Anyway"/`xattr`
instructions for the Mac and the Smart App Control note for Windows (keep the SmartScreen note until reputation
builds).

## Pull requests

Run `make test` and `make render`, look at the renders, and include a screenshot when you change anything
visual. If you touch the core, also run `make crosscheck`. If you touch Windows code, build it (MSVC or mingw)
and run `core_tests` and a few `/render`s; CI uploads Windows renders as the `renders-windows` artifact.
