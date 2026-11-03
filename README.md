# BibleTime

A macOS screen saver that tells time through scripture. The chapter is the hour and the verse is the minute:
at 3:16 you'll see John 3:16, at 4:13 Philippians 4:13, and a new verse appears every minute.

![BibleTime, Clock layout, Paper theme](docs/clock-paper.png)

It is inspired by the [Bible Clock](https://bibleclock.com) hardware clock, which does the same thing on a
7-inch display in a wooden frame. BibleTime is an independent open-source project and is not affiliated with
Bible Clock.

## Features

- **Three public-domain translations**, bundled and offline: King James Version, World English Bible, and
  American Standard Version.
- **Well-known verses first.** When a famous verse matches the minute (John 3:16, Philippians 4:13,
  Hebrews 11:1, Proverbs 3:5, Romans 8:28, Matthew 6:33 ...) it takes priority. Otherwise a verse is drawn
  from every book that has that chapter and verse, rotating daily so every Mac shows the same verse at the
  same minute.
- **Whole thoughts.** A verse that ends mid-sentence is extended with the next verse
  ("Romans 08:38–39") so what you read makes sense on its own. Genealogies, name lists and census tables
  are never shown.
- **Top of the hour** shows a verse about time and seasons (Psalm 90:12, Ecclesiastes 3:1, Psalm 118:24 ...),
  since there is no verse 0.
- **12- or 24-hour.** Follows your system clock setting; in 24-hour mode 15:16 prefers a chapter 15 verse.
- **Three layouts** (Clock, Centered, Book), **two themes** (Paper, Night, or automatic by appearance and time
  of day), five typefaces, an adjustable text size, an optional small clock.
- **Easy on the screen.** One crossfade per minute, a few points of drift every five minutes, and nothing else
  moving. Text is laid out in points and scales to any display, Retina or not, portrait or ultrawide.
- Builds with the Swift compiler from Xcode Command Line Tools alone. No Xcode needed.

| Centered, Paper | Book, Night |
| --- | --- |
| ![Centered layout](docs/centered-paper.png) | ![Book layout](docs/book-night.png) |

## Install

1. Download `BibleTime-<version>.zip` from the [latest release](https://github.com/calebtonn/BibleTime/releases/latest) and unzip it.
2. BibleTime is signed ad hoc, not notarized (that needs a paid Apple developer account), so macOS will refuse
   to open the downloaded bundle at first. Either:
   - double-click `BibleTime.saver`, click **Done** on the warning, then open **System Settings ›
     Privacy & Security**, scroll down and click **Open Anyway**; or
   - remove the quarantine flag in Terminal, then double-click the bundle:
     ```bash
     xattr -dr com.apple.quarantine ~/Downloads/BibleTime.saver
     ```
3. Choose **Install for this user only** (or copy the bundle into `~/Library/Screen Savers/` yourself).
4. Pick BibleTime in **System Settings › Wallpaper › Screen Saver** and click **Options…** to adjust it.

If you trust the source more than the binary, build it yourself; it takes a few seconds.

## Build from source

```bash
git clone https://github.com/calebtonn/BibleTime.git
cd BibleTime
make install
```

`make` produces `build/BibleTime.saver` (universal, arm64 + x86_64). `make test` runs the unit tests, and
`make render` writes screenshots of every layout to `build/renders/` without touching System Settings. If you
prefer Xcode, `make xcodeproj` generates a project with [XcodeGen](https://github.com/yonaskolb/XcodeGen).
See [CONTRIBUTING.md](CONTRIBUTING.md) for the development loop.

## Options

| Setting | Values | Default |
| --- | --- | --- |
| Translation | KJV, WEB, ASV, or any you import | KJV |
| Hours | Follow System, 12-hour, 24-hour | Follow System |
| Appearance | Automatic, Paper, Night | Automatic (Night in dark mode or 8 pm–7 am) |
| Layout | Clock, Centered, Book | Clock |
| Typeface | New York, Charter, Palatino, Georgia, San Francisco | New York |
| Text size | 75%–200% | 100% (4% of the screen height) |
| Show the time | on/off | on |
| Prefer well-known verses | on/off | on |
| Complete the sentence | on/off | on |
| Pad the reference like a clock | on/off | on ("John 03:16") |

## Other translations (ESV, CSB, LSB, NIV …)

Modern translations are copyrighted, so BibleTime cannot ship them. If you hold a copy you are licensed to
use (for example a USFX export of a module you bought), you can convert it and import it for your own use:

```bash
python3 -I Tools/build_verses.py build --source my-esv.usfx.xml --id esv \
    --name "English Standard Version" --abbreviation ESV \
    --publisher Crossway --license "Copyright 2001 Crossway. Personal use only." --out ~/Desktop/esv.json
```

Then open **Options…**, click **Import…** next to the translation menu, and choose the file. The import is
checked (whole Bible, USFM book codes, a unique id) and copied into the screen saver's own folder; nothing is
sent anywhere. **Remove** deletes it again. You can also drop a converted file into
`~/Library/Application Support/BibleTime/Translations/` by hand.

The conversion runs on your machine and the result stays there. Please do not commit converted copyrighted
text to this repository or share it; respect the publisher's terms.

## How a verse is chosen

For a time `H:MM`:

1. If `MM` is `00`, show a verse about time from `Resources/hour_verses.json`, walking through the list hour
   by hour.
2. Otherwise collect every verse numbered `H:MM` in the chosen translation (chapter 1–12, or 13–23 first in
   24-hour mode). The KJV has 12,858 such verses that read well on their own for the 12-hour dial, and 20,674 for 24 hours.
3. If "Prefer well-known verses" is on and `Resources/curated.json` lists verses for this minute, show one of
   them, rotating by day.
4. Otherwise pick at random, seeded by the date and minute, among verses that read well standalone.
   Verses flagged at build time as weak (a trailing comma, a dialogue tag) are used only when nothing better
   exists; verses flagged as lists (genealogies, rosters, census counts) are never used.
5. If the verse ends with a comma or dash and "Complete the sentence" is on, append the next verse or two.
6. Thirty-two minutes on the 12-hour dial have no usable verse in the KJV (2:53–2:59, 4:55–4:59,
   5:49–5:59, 10:53–10:59, 11:58 and 11:59: the only chapters that long there are censuses and
   genealogies). Those show a verse about time as well.

All of this lives in `Sources/BibleTimeCore` and is covered by `make test`.

## Scripture text

The translations come from [eBible.org](https://ebible.org) and are public domain; the exact editions,
download dates and checksums are recorded in [DATA-LICENSE.md](DATA-LICENSE.md). `Tools/build_verses.py`
turns the USFX source files into the compact JSON in `Resources/Translations/`, dropping Psalm titles,
footnotes and Strong's numbers, and flags verses that read poorly on their own.

## Credits and prior art

- [Bible Clock](https://bibleclock.com) for the idea of telling time through scripture.
- Other verse clocks whose choices informed this one: [VerseTime](https://github.com/TroyWolf/VerseTime),
  [verse-clock](https://github.com/NJShell2/verse-clock),
  [Bible-clock-widget](https://github.com/Halo225/Bible-clock-widget),
  [MMM-BibleVerseClock](https://github.com/Tydyrium/MMM-BibleVerseClock) and
  [markyharris/bible_clock](https://github.com/markyharris/bible_clock).
- [ScreenSaverMinimal](https://github.com/mirkofetter/ScreenSaverMinimal) and
  [Aerial](https://github.com/JohnCoates/Aerial) for documenting the quirks of `legacyScreenSaver`.

## License

The code, build scripts and artwork are MIT licensed; see [LICENSE](LICENSE). That license does not cover
the Scripture text in `Resources/Translations/`, which is public domain; its provenance is recorded in
[DATA-LICENSE.md](DATA-LICENSE.md). "World English Bible" is a trademark of eBible.org; the text is shipped
unmodified apart from the formatting described there.
