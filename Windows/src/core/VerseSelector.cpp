// Verse selection. Mirrors Sources/BibleTimeCore/VerseSelector.swift step for step: the random number stream must
// be consumed in exactly the same order as on the Mac, or the two would show different verses for the same minute.
#include <algorithm>
#include <climits>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#include "Core.h"

namespace bt {

// MARK: - CuratedVerses

namespace {

/// RefRow: [book, chapter, verse, ...] (extra elements ignored, like Swift's unkeyed decoding).
VerseRef decodeRefRow(const json::Value& row) {
    if (!row.isArray() || row.items().size() < 3) throw std::runtime_error("curated row must be [book, chapter, verse]");
    const auto& items = row.items();
    if (!items[0].isString() || !items[1].isInteger() || !items[2].isInteger() || items[1].asInt() < INT_MIN ||
        items[1].asInt() > INT_MAX || items[2].asInt() < INT_MIN || items[2].asInt() > INT_MAX) {
        throw std::runtime_error("curated row must be [book, chapter, verse]");
    }
    return VerseRef(items[0].asString(), static_cast<int>(items[1].asInt()), static_cast<int>(items[2].asInt()));
}

std::vector<VerseRef> decodeRefRows(const json::Value& rows) {
    if (!rows.isArray()) throw std::runtime_error("curated entry must be an array of rows");
    std::vector<VerseRef> refs;
    refs.reserve(rows.items().size());
    for (const json::Value& row : rows.items()) refs.push_back(decodeRefRow(row));
    return refs;
}

}  // namespace

CuratedVerses CuratedVerses::fromJSON(std::string_view curatedText, std::string_view hoursText) {
    const json::Value slots = json::parse(curatedText);
    if (!slots.isObject()) throw std::runtime_error("curated.json must be an object");
    CuratedVerses result;
    for (const auto& member : slots.members()) {
        // Decode first: a malformed row fails the whole file even when its key is skipped (as in Swift).
        std::vector<VerseRef> refs = decodeRefRows(member.second);
        // key.split(separator: ":") drops empty pieces, so "3::16" still has two parts.
        std::vector<std::string_view> parts;
        std::string_view key = member.first;
        size_t start = 0;
        for (size_t i = 0; i <= key.size(); ++i) {
            if (i == key.size() || key[i] == ':') {
                if (i > start) parts.push_back(key.substr(start, i - start));
                start = i + 1;
            }
        }
        if (parts.size() != 2) continue;
        const auto h = text::parseInt(parts[0]);
        const auto m = text::parseInt(parts[1]);
        if (!h || !m || *h < 1 || *h > 12 || *m < 0 || *m > 59) continue;
        result.bySlot[static_cast<int>(*h * 60 + *m)] = std::move(refs);
    }
    result.hourVerses = decodeRefRows(json::parse(hoursText));
    return result;
}

// MARK: - Selection

const char* rawValue(SelectionKind kind) {
    switch (kind) {
        case SelectionKind::Match: return "match";
        case SelectionKind::TopOfHour: return "topOfHour";
        case SelectionKind::Fallback: return "fallback";
    }
    return "match";
}

std::string Selection::text() const {
    std::string out;
    for (size_t i = 0; i < verses.size(); ++i) {
        if (i) out += ' ';
        out += verses[i]->text;
    }
    return out;
}

bool Selection::operator==(const Selection& o) const {
    if (kind != o.kind || verses.size() != o.verses.size()) return false;
    for (size_t i = 0; i < verses.size(); ++i) {
        const Verse& a = *verses[i];
        const Verse& b = *o.verses[i];
        if (a.ref != b.ref || a.text != b.text || a.flags != b.flags) return false;
    }
    return true;
}

// MARK: - ClockTime

namespace {

/// Julian day number of a proleptic Gregorian (or, when `julian`, Julian calendar) civil date.
int64_t julianDayNumber(int64_t year, int64_t month, int64_t day, bool julian) {
    const int64_t a = (14 - month) / 12;
    const int64_t y = year + 4800 - a;
    const int64_t m = month + 12 * a - 3;
    // Floor division keeps this right for years before -4800 too.
    const auto fdiv = [](int64_t n, int64_t d) { return n >= 0 ? n / d : -((-n + d - 1) / d); };
    if (julian) return day + (153 * m + 2) / 5 + 365 * y + fdiv(y, 4) - 32083;
    return day + (153 * m + 2) / 5 + 365 * y + fdiv(y, 4) - fdiv(y, 100) + fdiv(y, 400) - 32045;
}

}  // namespace

int64_t ClockTime::dayOrdinalFor(int year, int month, int day) {
    // Foundation's Gregorian calendar is Julian before 1582-10-15. Its ordinality(of: .day, in: .era) is the
    // Julian day number minus 1721424 (so 0001-01-01 Julian is 0 and 1970-01-01 is 719164). The offset is pinned by
    // the Mac's own output: the Swift tests expect John 3:16 at 3:16 PM on 2026-11-03 (ordinal 739924, not 739925),
    // and the committed docs/book-night.png shows Mark 9:16, which only ordinal 739924 produces that week.
    // Tests/CrossCheck compares this function with Foundation over 2000 dates in CI.
    // Dates in the nonexistent 1582-10-05...14 gap are read as Julian.
    const bool gregorian = year > 1582 || (year == 1582 && (month > 10 || (month == 10 && day >= 15)));
    return julianDayNumber(year, month, day, !gregorian) - 1721424;
}

// MARK: - SeededGenerator

SeededGenerator::SeededGenerator(const ClockTime& time, std::string_view salt) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : salt) h = (h ^ static_cast<uint64_t>(static_cast<unsigned char>(c))) * 0x100000001b3ull;
    const uint64_t mixed = (static_cast<uint64_t>(time.dayOrdinal) * 0x9E3779B97F4A7C15ull) ^
                           (static_cast<uint64_t>(time.minuteKey()) << 20) ^ h;
    state_ = mixed;
}

uint64_t SeededGenerator::next() {
    state_ += 0x9E3779B97F4A7C15ull;
    uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

namespace {

/// Full 64x64 -> 128-bit product, like Swift's multipliedFullWidth(by:).
void multiplyFullWidth(uint64_t a, uint64_t b, uint64_t& high, uint64_t& low) {
#if defined(__SIZEOF_INT128__)
    const unsigned __int128 product = static_cast<unsigned __int128>(a) * b;
    high = static_cast<uint64_t>(product >> 64);
    low = static_cast<uint64_t>(product);
#elif defined(_MSC_VER) && defined(_M_X64)
    low = _umul128(a, b, &high);
#elif defined(_MSC_VER) && defined(_M_ARM64)
    low = a * b;
    high = __umulh(a, b);
#else
    const uint64_t aLo = a & 0xFFFFFFFFu, aHi = a >> 32, bLo = b & 0xFFFFFFFFu, bHi = b >> 32;
    const uint64_t ll = aLo * bLo, lh = aLo * bHi, hl = aHi * bLo, hh = aHi * bHi;
    const uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
    high = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    low = (mid << 32) | (ll & 0xFFFFFFFFu);
#endif
}

}  // namespace

uint64_t SeededGenerator::next(uint64_t upperBound) {
    // Swift's RandomNumberGenerator.next(upperBound:) (Lemire's nearly divisionless method).
    uint64_t random = next();
    uint64_t high, low;
    multiplyFullWidth(random, upperBound, high, low);
    if (low < upperBound) {
        const uint64_t t = (0 - upperBound) % upperBound;
        while (low < t) {
            random = next();
            multiplyFullWidth(random, upperBound, high, low);
        }
    }
    return high;
}

// MARK: - VerseSelector

namespace {

/// Array.randomElement(using:): nil for an empty array without touching the generator.
const Verse* randomElement(const std::vector<const Verse*>& items, SeededGenerator& rng) {
    if (items.empty()) return nullptr;
    return items[rng.randomIndex(items.size())];
}

bool contains(const VerseRefSet& set, const VerseRef& ref) { return set.find(ref) != set.end(); }

}  // namespace

std::vector<int> VerseSelector::chapters(const ClockTime& time, bool use24Hour) {
    if (use24Hour && time.hour >= 13) return {time.hour, time.hour12()};
    return {time.hour12()};
}

std::optional<Selection> VerseSelector::select(const ClockTime& time, const Preferences& preferences, bool use24Hour,
                                               int variant) const {
    variant = std::max(0, variant);
    VerseRefSet taken;
    for (int v = 0; v < variant; ++v) {
        if (auto earlier = selectOne(time, preferences, use24Hour, v, taken)) taken.insert(earlier->primary().ref);
    }
    return selectOne(time, preferences, use24Hour, variant, taken);
}

std::optional<Selection> VerseSelector::selectOne(const ClockTime& time, const Preferences& preferences, bool use24Hour,
                                                  int variant, const VerseRefSet& taken) const {
    SeededGenerator rng(time, variant == 0 ? translation_.id() : translation_.id() + "#" + std::to_string(variant));
    const auto make = [&](const Verse* verse, SelectionKind kind) {
        Selection s;
        s.verses = extended(*verse, preferences.extendFragments);
        s.kind = kind;
        return s;
    };

    if (time.minute == 0) {
        if (const Verse* verse = hourVerse(time, variant, rng)) return make(verse, SelectionKind::TopOfHour);
    }
    for (const int chapter : chapters(time, use24Hour)) {
        if (const Verse* verse = pick(chapter, time.minute, time, preferences, variant, taken, rng)) {
            return make(verse, SelectionKind::Match);
        }
    }
    if (const Verse* verse = hourVerse(time, variant, rng)) return make(verse, SelectionKind::Fallback);
    return std::nullopt;
}

const Verse* VerseSelector::pick(int chapter, int minute, const ClockTime& time, const Preferences& preferences,
                                 int variant, const VerseRefSet& taken, SeededGenerator& rng) const {
    const auto& candidates = translation_.verses(chapter, minute);
    if (candidates.empty()) return nullptr;

    if (preferences.preferFamous && chapter == time.hour12()) {
        std::vector<const Verse*> famous;
        const auto it = curated_.bySlot.find(time.hour12() * 60 + minute);
        if (it != curated_.bySlot.end()) {
            for (const VerseRef& ref : it->second) {
                if (const Verse* v = translation_.find(ref)) famous.push_back(v);
            }
        }
        if (!famous.empty()) {
            const int count = static_cast<int>(famous.size());
            const size_t index = static_cast<size_t>((famousIndex(count, time.dayOrdinal) + variant) % count);
            const bool allTaken =
                std::all_of(famous.begin(), famous.end(), [&](const Verse* v) { return contains(taken, v->ref); });
            if (!contains(taken, famous[index]->ref) || allTaken) return famous[index];
            // The shifted famous verse is already on another display; fall through to the general pool.
        }
    }

    // A verse that merely ends mid-sentence is as good as a clean one once the following verse completes it.
    const auto extendable = [&](const Verse* verse) {
        if (!preferences.extendFragments || verse->flags != FlagPenalize || !verse->endsMidSentence()) return false;
        const auto chain = extended(*verse, true);
        return chain.size() > 1 && !chain.back()->endsMidSentence();
    };
    std::vector<const Verse*> clean, weak;
    for (const Verse* v : candidates) {
        if (v->flags == 0 || extendable(v)) clean.push_back(v);
    }
    // Genealogies and name lists (exclude) are never shown; the caller folds to another chapter or a time verse.
    for (const Verse* v : candidates) {
        if (!(v->flags & FlagExclude)) weak.push_back(v);
    }
    // Verses no other display is showing come first, clean before weak; only then repeat one.
    const auto notTaken = [&](const std::vector<const Verse*>& tier) {
        std::vector<const Verse*> out;
        for (const Verse* v : tier) {
            if (!contains(taken, v->ref)) out.push_back(v);
        }
        return out;
    };
    if (const Verse* v = randomElement(notTaken(clean), rng)) return v;
    if (const Verse* v = randomElement(notTaken(weak), rng)) return v;
    if (const Verse* v = randomElement(clean, rng)) return v;
    if (const Verse* v = randomElement(weak, rng)) return v;
    return nullptr;
}

int VerseSelector::famousIndex(int count, int64_t dayOrdinal) {
    if (count <= 1) return 0;
    const int64_t period = 3 * (int64_t{count} - 1);
    const int64_t day = ((dayOrdinal % period) + period) % period;
    if (day % 3 != 2) return 0;
    return static_cast<int>(1 + (day / 3) % (count - 1));
}

const Verse* VerseSelector::hourVerse(const ClockTime& time, int offset, SeededGenerator& rng) const {
    std::vector<const Verse*> pool;
    pool.reserve(curated_.hourVerses.size());
    for (const VerseRef& ref : curated_.hourVerses) {
        if (const Verse* v = translation_.find(ref)) pool.push_back(v);
    }
    if (pool.empty()) return nullptr;
    if (time.minute == 0) {
        // Walk the list hour by hour so a day cycles through most of it, offset by the day for variety
        // (and by the display number, so screens showing different verses differ here too).
        const auto count = static_cast<int64_t>(pool.size());
        const int64_t index = ((time.dayOrdinal * 7 + time.hour + offset) % count + count) % count;
        return pool[static_cast<size_t>(index)];
    }
    return randomElement(pool, rng);
}

std::vector<const Verse*> VerseSelector::extended(const Verse& verse, bool enabled) const {
    std::vector<const Verse*> verses{&verse};
    if (!enabled) return verses;
    size_t total = characterCount(verse.text);
    while (verses.size() < 3 && verses.back()->endsMidSentence()) {
        const Verse* next = translation_.find(verses.back()->ref.next());
        if (!next || next->text.empty()) break;
        const size_t length = characterCount(next->text);
        if (total + length > 420) break;
        verses.push_back(next);
        total += length;
    }
    return verses;
}

// MARK: - ReferenceFormatter

std::string ReferenceFormatter::string(const Selection& selection) const {
    const auto number = [this](int n) { return pad_ && n < 10 ? "0" + std::to_string(n) : std::to_string(n); };
    const VerseRef& first = selection.primary().ref;
    const Book* book = canon_.book(first.book);
    const std::string& name = book ? book->name : first.book;
    std::string verses = number(first.verse);
    const VerseRef& last = selection.verses.back()->ref;
    if (selection.isExtended() && last.chapter == first.chapter) verses += "–" + number(last.verse);
    return name + " " + number(first.chapter) + ":" + verses;
}

}  // namespace bt
