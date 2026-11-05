// BibleTime core for Windows: a faithful C++17 port of Sources/BibleTimeCore (Swift).
//
// The verse chosen for a minute must be identical to the macOS screen saver's, so every algorithm here mirrors the
// Swift source line by line (SplitMix64 seeding, Swift's Lemire-style bounded random, the famous-verse rotation, the
// tiered pick, the fragment extension). Tools/crosscheck compares both implementations over every minute of several
// days in CI. When changing either side, change both.
//
// Strings are UTF-8 throughout. Paths are std::filesystem::path (wide on Windows).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Json.h"

namespace bt {

// MARK: - Canon

/// One book of the Protestant 66-book canon, as shipped in Resources/books.json.
struct Book {
    std::string code;       // USFM code, e.g. "JHN"
    std::string name;       // "John", "Psalm", "1 John"
    std::string shortName;  // "Jn"  (JSON key "short")
    int chapters = 0;
};

struct VerseRef {
    std::string book;
    int chapter = 0;
    int verse = 0;

    VerseRef() = default;
    VerseRef(std::string b, int c, int v) : book(std::move(b)), chapter(c), verse(v) {}
    VerseRef next() const { return VerseRef(book, chapter, verse + 1); }
    /// "JHN 3:16"
    std::string description() const;
    bool operator==(const VerseRef& o) const { return chapter == o.chapter && verse == o.verse && book == o.book; }
    bool operator!=(const VerseRef& o) const { return !(*this == o); }
};

struct VerseRefHash {
    size_t operator()(const VerseRef& r) const noexcept;
};

using VerseRefSet = std::unordered_set<VerseRef, VerseRefHash>;

class Canon {
public:
    Canon() = default;
    explicit Canon(std::vector<Book> books);
    /// Decode books.json (an array of {code,name,short,chapters}). Throws std::runtime_error.
    static Canon fromJSON(std::string_view data);

    const std::vector<Book>& books() const { return books_; }
    const Book* book(std::string_view code) const;
    /// Canonical position (0 = Genesis) or -1 when unknown.
    int order(std::string_view code) const;
    /// Parse "1 John 4:8", "Psalm 23:1", "Song of Solomon 2:4". nullopt when unknown.
    std::optional<VerseRef> parse(std::string_view reference) const;
    /// Find a book by display name, short name, code or alias, case-insensitively (ASCII).
    const Book* bookNamed(std::string_view name) const;

private:
    std::vector<Book> books_;
    std::unordered_map<std::string, int> byCode_;
};

// MARK: - Translations

enum VerseFlag : int {
    /// Genealogies, name lists. Never shown.
    FlagExclude = 1,
    /// Weaker standalone (trailing comma, dialogue tag, very short).
    FlagPenalize = 2,
};

struct Verse {
    VerseRef ref;
    std::string text;
    int flags = 0;

    /// Ends with ',', U+2014, '-', or U+2013 (last Unicode scalar).
    bool endsMidSentence() const;
};

/// Number of user-perceived characters, matching Swift's String.count for the texts BibleTime ships
/// (Unicode scalars, not counting combining marks, variation selectors or ZWJ continuations; CR LF counts once).
size_t characterCount(std::string_view utf8);

class Translation {
public:
    /// Parse a translation file ({id,name,abbreviation,language?,source?,verses:[[book,ch,v,text,flags?],...]}).
    /// Throws std::runtime_error (json::ParseError or a description of the missing field).
    static std::shared_ptr<Translation> fromJSON(std::string_view data, const Canon& canon,
                                                 std::filesystem::path sourcePath = {});

    const std::string& id() const { return id_; }  // normalized: trimmed, lowercased
    const std::string& name() const { return name_; }
    const std::string& abbreviation() const { return abbreviation_; }
    const std::string& language() const { return language_; }
    /// source.license when present, else empty.
    const std::string& license() const { return license_; }
    /// Number of distinct references.
    size_t verseCount() const { return byRef_.size(); }
    const std::filesystem::path& sourcePath() const { return sourcePath_; }
    const std::unordered_set<std::string>& bookCodes() const { return bookCodes_; }

    /// nullptr when the reference is not in the file.
    const Verse* find(const VerseRef& ref) const;
    /// Every verse numbered chapter:verse (both 1...99) across books, in canonical order. Empty otherwise.
    const std::vector<const Verse*>& verses(int chapter, int verse) const;

private:
    Translation() = default;
    std::string id_, name_, abbreviation_, language_ = "en", license_;
    std::filesystem::path sourcePath_;
    std::vector<Verse> storage_;
    std::unordered_map<VerseRef, size_t, VerseRefHash> byRef_;
    std::vector<std::vector<const Verse*>> bySlot_;  // index chapter*100+verse, size 10000
    std::unordered_set<std::string> bookCodes_;
};

struct TranslationInfo {
    std::string id, name, abbreviation;
    /// Empty for bundled translations (they come from the resource loader).
    std::filesystem::path path;
    bool isImported = false;
    /// "World English Bible (WEB)"
    std::string displayName() const { return name + " (" + abbreviation + ")"; }
    bool operator==(const TranslationInfo& o) const {
        return id == o.id && name == o.name && abbreviation == o.abbreviation && path == o.path && isImported == o.isImported;
    }
};

struct TranslationHeader {
    std::string id, name, abbreviation;
    bool operator==(const TranslationHeader& o) const { return id == o.id && name == o.name && abbreviation == o.abbreviation; }
    /// Trim ASCII whitespace and lowercase.
    static std::string normalizeID(std::string_view id);
    /// Fast path reads the first 4 KB and cuts at "verses"; otherwise parses the whole file.
    /// Throws ImportError(invalidFormat) when id, name or abbreviation is missing.
    static TranslationHeader fromFile(const std::filesystem::path& path);
};

/// Supplies bundled resource files by relative name: "books.json", "curated.json", "hour_verses.json",
/// "Translations/kjv.json". Returns nullopt when missing. On Windows these are embedded RCDATA resources;
/// tests use a directory.
using ResourceLoader = std::function<std::optional<std::string>(const std::string& name)>;

/// A loader that reads files under `directory`.
ResourceLoader directoryLoader(std::filesystem::path directory);

class TranslationCatalog {
public:
    struct BundledEntry { const char* id; const char* name; const char* abbreviation; };
    /// kjv, web, asv.
    static const std::vector<BundledEntry>& bundled();
    static bool isBundledID(std::string_view id);

    TranslationCatalog(const Canon& canon, ResourceLoader loader, std::vector<std::filesystem::path> importDirectories);

    /// Bundled first (those the loader has), then imported sorted by name (case-insensitive).
    std::vector<TranslationInfo> available() const;
    std::optional<TranslationInfo> info(std::string_view id) const;
    const std::vector<std::filesystem::path>& importDirectories() const { return importDirectories_; }
    /// Re-read import directories; drops cached imported translations.
    void rescan();
    /// Load (cached). Falls back to the first available when `id` is unknown. nullptr when nothing is available.
    /// Throws when the file cannot be read or parsed.
    std::shared_ptr<const Translation> translation(std::string_view id);

private:
    const Canon& canon_;
    ResourceLoader loader_;
    std::vector<std::filesystem::path> importDirectories_;
    mutable std::mutex mutex_;
    std::vector<TranslationInfo> bundledFound_;  // bundled entries the loader has (probed once)
    std::vector<TranslationInfo> found_;
    std::unordered_map<std::string, std::shared_ptr<const Translation>> loaded_;
};

class ImportError : public std::runtime_error {
public:
    enum class Kind { InvalidFormat, BundledID, TooFewVerses, UnknownBooks };
    ImportError(Kind errorKind, std::string message, std::vector<std::string> bookCodes = {})
        : std::runtime_error(std::move(message)), kind(errorKind), codes(std::move(bookCodes)) {}
    static ImportError invalidFormat(const std::string& detail);
    static ImportError bundledID(const std::string& id);
    static ImportError tooFewVerses(size_t n);
    static ImportError unknownBooks(std::vector<std::string> sortedCodes);
    Kind kind;
    std::vector<std::string> codes;
};

class TranslationImporter {
public:
    TranslationImporter(const Canon& canon, std::filesystem::path destination)
        : canon_(canon), destination_(std::move(destination)) {}
    const std::filesystem::path& destination() const { return destination_; }
    /// Parse and sanity-check (id charset, name/abbreviation, not bundled, >= 5000 verses, known books).
    std::pair<TranslationHeader, size_t> validate(std::string_view data) const;
    /// Validate and copy to destination/<id>.json (via a .<id>.json.part temp file), replacing any previous import.
    TranslationInfo importFile(const std::filesystem::path& source) const;
    /// Delete an imported file; bundled entries are never touched.
    void remove(const TranslationInfo& info) const;

private:
    const Canon& canon_;
    std::filesystem::path destination_;
};

// MARK: - Preferences

enum class HourStyle { System, TwelveHour, TwentyFourHour };
enum class ThemeChoice { Auto, Paper, Night };
enum class LayoutStyle { Clock, Centered, Book };
enum class TypefaceChoice { NewYork, Charter, Palatino, Georgia, Sans };
enum class DisplayMode { MainOnly, SameVerse, DifferentVerses };

/// Raw values identical to the Swift enums ("system", "twelveHour", "twentyFourHour", "auto", "paper", "night",
/// "clock", "centered", "book", "newYork", "charter", "palatino", "georgia", "sans", "mainOnly", "sameVerse",
/// "differentVerses"), so settings files are interchangeable.
const char* rawValue(HourStyle);
const char* rawValue(ThemeChoice);
const char* rawValue(LayoutStyle);
const char* rawValue(TypefaceChoice);
const char* rawValue(DisplayMode);
std::optional<HourStyle> hourStyleFrom(std::string_view);
std::optional<ThemeChoice> themeFrom(std::string_view);
std::optional<LayoutStyle> layoutFrom(std::string_view);
std::optional<TypefaceChoice> typefaceFrom(std::string_view);
std::optional<DisplayMode> displayModeFrom(std::string_view);
/// English titles shown in the options dialog ("Follow System", "Automatic", "Main display only", ...).
/// Typeface titles are platform specific and live in the app (see Typeface.h).
const char* title(HourStyle);
const char* title(ThemeChoice);
const char* title(LayoutStyle);
const char* title(DisplayMode);
template <class E> const std::vector<E>& allCases();
template <> const std::vector<HourStyle>& allCases<HourStyle>();
template <> const std::vector<ThemeChoice>& allCases<ThemeChoice>();
template <> const std::vector<LayoutStyle>& allCases<LayoutStyle>();
template <> const std::vector<TypefaceChoice>& allCases<TypefaceChoice>();
template <> const std::vector<DisplayMode>& allCases<DisplayMode>();

struct Preferences {
    std::string translation = "kjv";
    HourStyle hourStyle = HourStyle::System;
    ThemeChoice theme = ThemeChoice::Auto;
    LayoutStyle layout = LayoutStyle::Clock;
    TypefaceChoice typeface = TypefaceChoice::NewYork;
    double textScale = 1.0;
    bool showClock = true;
    bool preferFamous = true;
    bool extendFragments = true;
    bool padReference = true;
    DisplayMode displayMode = DisplayMode::SameVerse;

    static constexpr double textScaleMin = 0.75;
    static constexpr double textScaleMax = 2.0;

    Preferences normalized() const;
    /// Keys as in Swift's Preferences.Key. Missing or malformed keys keep defaults; result is normalized.
    static Preferences fromJSON(const json::Value& object);
    /// Parse text; malformed JSON yields defaults.
    static Preferences fromJSONText(std::string_view text);
    json::Value toJSON() const;
    std::string toJSONText() const;  // pretty-printed

    /// `localeUses24Hour` comes from the OS locale (Windows: LOCALE_STIMEFORMAT contains 'H').
    bool uses24Hour(bool localeUses24Hour) const;

    bool operator==(const Preferences& o) const;
    bool operator!=(const Preferences& o) const { return !(*this == o); }
};

// MARK: - Selection

struct CuratedVerses {
    /// Keyed by hour12 * 60 + minute (hour 1–12), most famous first.
    std::unordered_map<int, std::vector<VerseRef>> bySlot;
    std::vector<VerseRef> hourVerses;
    /// curated: {"H:MM": [["JHN",3,16], ...]}; hours: [["PSA",90,12], ...]. Throws on malformed JSON.
    static CuratedVerses fromJSON(std::string_view curated, std::string_view hours);
};

enum class SelectionKind { Match, TopOfHour, Fallback };
const char* rawValue(SelectionKind);  // "match", "topOfHour", "fallback"

struct Selection {
    std::vector<const Verse*> verses;  // non-empty
    SelectionKind kind = SelectionKind::Match;
    const Verse& primary() const { return *verses.front(); }
    /// Verse texts joined by a single space.
    std::string text() const;
    bool isExtended() const { return verses.size() > 1; }
    bool operator==(const Selection& o) const;
};

struct ClockTime {
    int hour = 0;    // 0–23
    int minute = 0;  // 0–59
    /// Days since an epoch in the local calendar; equals Foundation's
    /// Calendar(.gregorian).ordinality(of: .day, in: .era, for:) so Mac and PC agree on the daily rotation.
    int64_t dayOrdinal = 0;

    int hour12() const { return hour % 12 == 0 ? 12 : hour % 12; }
    int minuteKey() const { return hour * 60 + minute; }

    /// Ordinal for a UTC civil date, equal to Foundation's Calendar(.gregorian).ordinality(of: .day, in: .era), which on
    /// macOS counts UTC days of the instant regardless of the calendar's time zone. So the daily rotation turns over at
    /// 00:00 UTC on both platforms. julianDayNumber(y, m, d) - 1721425 (1970-01-01 -> 719163, 2026-11-03 -> 739923).
    static int64_t dayOrdinalFor(int year, int month, int day);
    /// The ordinal for an instant: floor(seconds / 86400) + 719163.
    static int64_t dayOrdinalForUnixTime(int64_t secondsSince1970);
};

/// SplitMix64, seeded exactly like the Swift SeededGenerator.
class SeededGenerator {
public:
    explicit SeededGenerator(uint64_t seed) : state_(seed) {}
    SeededGenerator(const ClockTime& time, std::string_view salt);
    uint64_t next();
    /// Swift's RandomNumberGenerator.next(upperBound:) (Lemire's multiply-shift with rejection). upperBound > 0.
    uint64_t next(uint64_t upperBound);
    /// Swift's Array.randomElement(using:) index: next(upperBound: count). Requires count > 0.
    size_t randomIndex(size_t count) { return static_cast<size_t>(next(static_cast<uint64_t>(count))); }

private:
    uint64_t state_;
};

class VerseSelector {
public:
    VerseSelector(const Translation& translation, const CuratedVerses& curated)
        : translation_(translation), curated_(curated) {}

    static std::vector<int> chapters(const ClockTime& time, bool use24Hour);
    /// nullopt only when the translation has no usable verse at all.
    std::optional<Selection> select(const ClockTime& time, const Preferences& preferences, bool use24Hour,
                                    int variant = 0) const;
    static int famousIndex(int count, int64_t dayOrdinal);
    /// Append up to two following verses while the text ends mid-sentence and the total stays <= 420 characters.
    std::vector<const Verse*> extended(const Verse& verse, bool enabled) const;

private:
    std::optional<Selection> selectOne(const ClockTime& time, const Preferences& preferences, bool use24Hour,
                                       int variant, const VerseRefSet& taken) const;
    const Verse* pick(int chapter, int minute, const ClockTime& time, const Preferences& preferences, int variant,
                      const VerseRefSet& taken, SeededGenerator& rng) const;
    const Verse* hourVerse(const ClockTime& time, int offset, SeededGenerator& rng) const;

    const Translation& translation_;
    const CuratedVerses& curated_;
};

class ReferenceFormatter {
public:
    ReferenceFormatter(const Canon& canon, bool pad = true) : canon_(canon), pad_(pad) {}
    /// "John 03:16", "Romans 08:38–39" (U+2013), "Jude 01:24".
    std::string string(const Selection& selection) const;

private:
    const Canon& canon_;
    bool pad_;
};

// MARK: - Facade

struct DisplayContent {
    int minuteKey = 0;
    std::string text, reference, clock;
    SelectionKind kind = SelectionKind::Fallback;
    std::string translation;  // abbreviation
    bool operator==(const DisplayContent& o) const {
        return minuteKey == o.minuteKey && text == o.text && reference == o.reference && clock == o.clock &&
               kind == o.kind && translation == o.translation;
    }
    bool operator!=(const DisplayContent& o) const { return !(*this == o); }
};

/// Locale facts the core needs; the app fills these from the OS.
struct LocaleInfo {
    bool uses24Hour = false;
    std::string amSymbol = "AM";
    std::string pmSymbol = "PM";
};

/// "3:16 PM" (pattern "h:mm a") or "15:16" ("HH:mm").
std::string clockString(int hour, int minute, bool use24Hour, const LocaleInfo& locale);

class VerseClock {
public:
    struct LoadError : std::runtime_error { using std::runtime_error::runtime_error; };

    /// Loads books.json (required), the curated lists (optional, empty when missing or malformed) and scans
    /// translations. Throws LoadError("BibleTime resource missing: books.json") etc.
    VerseClock(ResourceLoader loader, std::vector<std::filesystem::path> importDirectories);
    // The catalog refers to canon_, so a VerseClock never moves or copies (hold it in a unique_ptr or optional).
    VerseClock(const VerseClock&) = delete;
    VerseClock& operator=(const VerseClock&) = delete;

    const Canon& canon() const { return canon_; }
    TranslationCatalog& catalog() { return *catalog_; }
    const CuratedVerses& curated() const { return curated_; }
    /// Imports into the first import directory; nullopt when there is none.
    std::optional<TranslationImporter> importer() const;

    /// Throws LoadError when no translation can be resolved.
    std::shared_ptr<const Translation> translation(const Preferences& preferences);
    DisplayContent content(const ClockTime& time, const Preferences& preferences, const LocaleInfo& locale,
                           int variant = 0);
    /// For screenshots: a specific verse. nullopt when the reference does not exist.
    std::optional<DisplayContent> content(const VerseRef& reference, const ClockTime& time,
                                          const Preferences& preferences, const LocaleInfo& locale);

private:
    Canon canon_;
    std::unique_ptr<TranslationCatalog> catalog_;
    CuratedVerses curated_;
};

// MARK: - Utilities

namespace text {
std::string toLowerASCII(std::string_view s);
std::string trimASCII(std::string_view s);  // spaces and tabs only (Swift .whitespaces), plus newlines when `newlines`
std::string trimASCII(std::string_view s, bool newlines);
/// Swift's Int(String): optional '+'/'-' then ASCII digits only; nullopt when malformed or out of int64 range.
std::optional<int64_t> parseInt(std::string_view s);
/// Read a whole file (binary). nullopt on failure.
std::optional<std::string> readFile(const std::filesystem::path& path);
}  // namespace text

}  // namespace bt
