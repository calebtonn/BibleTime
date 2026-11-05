// OS facts the screen saver needs: local time, locale, dark mode, settings storage, dev overrides.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "Core.h"
#include "Win32.h"

namespace bt::app {

/// Local wall-clock time as the core expects it.
ClockTime localClockTime();
/// Today's local date at the given hour and minute (for BIBLETIME_TIME).
ClockTime localClockTimeToday(int hour, int minute);

/// LOCALE_STIMEFORMAT contains 'H' -> 24-hour; AM/PM symbols from LOCALE_S1159 / LOCALE_S2359 (default "AM"/"PM").
LocaleInfo userLocale();

/// True when Windows apps use dark mode (HKCU\...\Themes\Personalize AppsUseLightTheme == 0).
bool appsUseDarkTheme();

/// Developer and CI hooks, mirroring Sources/BibleTime/DevOverrides.swift. Unset variables have no effect.
namespace dev {
/// BIBLETIME_TIME="15:16": today at that time.
std::optional<ClockTime> pinnedTime();
/// BIBLETIME_DISPLAY=1: pretend to be on that display (0 = main).
std::optional<int> pinnedDisplayIndex();
/// BIBLETIME_REF="EST 8:9" (USFM code) forces a verse.
std::optional<VerseRef> pinnedReference();
}  // namespace dev

/// The current time, honouring BIBLETIME_TIME.
ClockTime now();

/// %APPDATA%\BibleTime (created on demand by writers, not here). Empty if the known folder is unavailable.
std::filesystem::path appDataDirectory();
/// %APPDATA%\BibleTime\Translations
std::filesystem::path importDirectory();

/// Reads and writes Preferences as %APPDATA%\BibleTime\settings.json (the same keys and raw values as the Swift
/// Preferences, so a settings file is interchangeable). BIBLETIME_PREFS, when set, replaces the stored values.
class SettingsStore {
public:
    SettingsStore();
    const std::filesystem::path& path() const { return path_; }
    Preferences load() const;
    /// Atomic: writes settings.json.tmp, then MoveFileEx(REPLACE_EXISTING). Returns false on failure.
    bool save(const Preferences& preferences) const;
    /// True when settings.json changed (last-write time or existence) since the previous call. Cheap: one stat.
    bool changedSinceLastCheck();

private:
    std::filesystem::path path_;
    FILETIME lastWrite_{};
    bool existed_ = false;
};

}  // namespace bt::app
