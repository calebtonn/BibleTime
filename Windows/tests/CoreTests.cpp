// Core unit tests for the Windows port: every applicable check from Tests/CoreTests/main.swift, plus tests of the
// pieces that only exist in C++ (the JSON module, characterCount, dayOrdinalFor, Swift's next(upperBound:)).
//
//   core_tests <ResourcesDir>     prints failures, then "N passed, M failed"; exit code 0 when all pass.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "Core.h"
#include "Unicode.h"

#ifdef _WIN32
#include <process.h>
#define BT_GETPID _getpid
#else
#include <unistd.h>
#define BT_GETPID getpid
#endif

using namespace bt;
namespace fs = std::filesystem;

namespace {

int passes = 0;
int failures = 0;

void checkImpl(bool condition, const std::string& message, int line) {
    if (condition) {
        ++passes;
    } else {
        ++failures;
        std::printf("FAIL CoreTests.cpp:%d: %s\n", line, message.c_str());
    }
}

#define check(cond, msg) checkImpl(static_cast<bool>(cond), (msg), __LINE__)

void section(const char* name) { std::printf("\xE2\x80\x94 %s\n", name); }

std::string str(const VerseRef& r) { return r.description(); }
std::string str(const std::optional<VerseRef>& r) { return r ? r->description() : "nil"; }
std::string str(const std::optional<Selection>& s) { return s ? s->primary().ref.description() : "nil"; }

std::string join(const std::vector<std::string>& items, const char* separator = ", ") {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += separator;
        out += items[i];
    }
    return out;
}

std::vector<std::string> ids(const std::vector<TranslationInfo>& infos) {
    std::vector<std::string> out;
    for (const auto& i : infos) out.push_back(i.id);
    return out;
}

bool sameRef(const std::optional<Selection>& a, const std::optional<Selection>& b) {
    return a && b && a->primary().ref == b->primary().ref;
}

ClockTime at(int hour, int minute, int64_t day) {
    ClockTime t;
    t.hour = hour;
    t.minute = minute;
    t.dayOrdinal = day;
    return t;
}

/// The Swift tests build dates in a UTC Gregorian calendar; here a civil date maps to a ClockTime directly.
ClockTime date(int y, int mo, int d, int h, int mi) { return at(h, mi, ClockTime::dayOrdinalFor(y, mo, d)); }

bool writeText(const fs::path& path, const std::string& data) {
#ifdef _WIN32
    FILE* f = _wfopen(path.c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

template <class F> bool throwsParseError(F&& f) {
    try {
        f();
    } catch (const json::ParseError&) {
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path resources = fs::u8path(argc > 1 ? argv[1] : "Resources");
    const LocaleInfo us{false, "AM", "PM"};
    const LocaleInfo de{true, "AM", "PM"};

    std::unique_ptr<VerseClock> clockPtr;
    try {
        clockPtr = std::make_unique<VerseClock>(directoryLoader(resources), std::vector<fs::path>{});
    } catch (const std::exception& e) {
        std::printf("FAIL could not load resources at %s: %s\n", resources.u8string().c_str(), e.what());
        return 1;
    }
    VerseClock& clock = *clockPtr;
    const Canon& canon = clock.canon();

    section("canon");
    check(canon.books().size() == 66, "66 books, got " + std::to_string(canon.books().size()));
    check(canon.books().front().code == "GEN" && canon.books().back().code == "REV", "Genesis first, Revelation last");
    check(canon.book("PSA") && canon.book("PSA")->name == "Psalm", "Psalms displays as 'Psalm'");
    check(canon.parse("1 John 4:8") == VerseRef("1JN", 4, 8), "parse '1 John 4:8'");
    check(canon.parse("Psalms 23:1") == VerseRef("PSA", 23, 1), "parse 'Psalms 23:1' alias");
    check(canon.parse("Song of Solomon 2:4") && canon.parse("Song of Solomon 2:4")->book == "SNG", "parse 'Song of Solomon'");
    check(!canon.parse("Nowhere 1:1"), "unknown book is nil");
    check(!canon.parse("John"), "missing numbers is nil");
    check(canon.parse("  1 JOHN.  4:8 ") == VerseRef("1JN", 4, 8), "case, periods and padding are ignored: " + str(canon.parse("  1 JOHN.  4:8 ")));
    check(canon.parse("Gen 1:1") == VerseRef("GEN", 1, 1) && canon.parse("song of  songs 2:4") == VerseRef("SNG", 2, 4), "short names and aliases with extra spaces");
    check(canon.parse("III John 1:2") == VerseRef("3JN", 1, 2), "roman numeral alias");
    check(canon.parse("JHN 3:+16") == VerseRef("JHN", 3, 16), "code and Swift Int('+16')");
    check(!canon.parse("John 3:16a") && !canon.parse("John :16") && !canon.parse("John 3:"), "malformed numbers are nil");
    check(canon.order("GEN") == 0 && canon.order("REV") == 65 && canon.order("XYZ") == -1, "canonical order");

    section("translations");
    check(ids(clock.catalog().available()) == std::vector<std::string>({"kjv", "dby"}),
          "kjv, dby available: " + join(ids(clock.catalog().available())));
    std::vector<std::shared_ptr<const Translation>> translations;
    for (const auto& info : clock.catalog().available()) {
        try {
            const auto start = std::chrono::steady_clock::now();
            if (auto t = clock.catalog().translation(info.id)) translations.push_back(t);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::printf("   loaded %s in %.1f ms\n", info.id.c_str(), ms);
        } catch (const std::exception& e) {
            check(false, "load " + info.id + ": " + e.what());
        }
    }
    check(translations.size() == 2, "loaded 2 translations");
    for (const auto& t : translations) {
        check(t->verseCount() > 20000, t->id() + " has " + std::to_string(t->verseCount()) + " verses");
        const Verse* john = t->find(VerseRef("JHN", 3, 16));
        check(john && john->text.rfind("For God so loved the world", 0) == 0, t->id() + " John 3:16: " + (john ? john->text : "nil"));
        check(john && john->flags == 0, t->id() + " John 3:16 unflagged");
        const Verse* psalm = t->find(VerseRef("PSA", 23, 1));
        check(psalm && psalm->text.find("A Psalm") == std::string::npos, t->id() + " Psalm 23:1 has no superscription: " + (psalm ? psalm->text : "nil"));
        const auto& slot = t->verses(3, 16);
        check(std::any_of(slot.begin(), slot.end(), [](const Verse* v) { return v->ref.book == "JHN"; }), t->id() + " slot 3:16 includes John");
        check(slot.size() > 20, t->id() + " slot 3:16 has many candidates: " + std::to_string(slot.size()));
        check(t->verses(0, 16).empty() && t->verses(3, 0).empty(), t->id() + " no chapter/verse zero");
        check(t->verses(100, 1).empty() && t->verses(1, 100).empty() && t->verses(-1, 5).empty(), t->id() + " out-of-range slots are empty");
        const auto& ordered = t->verses(1, 1);
        check(!ordered.empty() && ordered.front()->ref.book == "GEN" && ordered.back()->ref.book == "REV", t->id() + " slot results in canonical order");
        bool sorted = true;
        for (size_t i = 1; i < ordered.size(); ++i) sorted = sorted && canon.order(ordered[i - 1]->ref.book) < canon.order(ordered[i]->ref.book);
        check(sorted, t->id() + " slot 1:1 strictly canonical");
        check(t->license() == "Public Domain", t->id() + " marked public domain");
        check(t->bookCodes().size() == 66, t->id() + " has 66 book codes");
        check(t->language() == "en", t->id() + " language");
    }
    const std::shared_ptr<const Translation> kjvPtr = translations.empty() ? nullptr : translations.front();
    if (!kjvPtr || kjvPtr->id() != "kjv") {
        std::printf("FAIL KJV did not load\n");
        return 1;
    }
    const Translation& kjv = *kjvPtr;
    {
        const auto f = [&](const char* b, int c, int v) { return kjv.find(VerseRef(b, c, v)); };
        check(f("MAT", 1, 2) && (f("MAT", 1, 2)->flags & FlagExclude), "KJV Matthew 1:2 (genealogy) excluded");
        check(f("EXO", 1, 2) && (f("EXO", 1, 2)->flags & FlagExclude), "KJV Exodus 1:2 (name list) excluded");
        check(f("JHN", 11, 35) && f("JHN", 11, 35)->text == "Jesus wept.", "KJV John 11:35");
        check(f("JHN", 11, 35) && f("JHN", 11, 35)->flags == 0, "KJV 'Jesus wept.' is whitelisted despite being short");
        check(f("ROM", 8, 38) && f("ROM", 8, 38)->endsMidSentence(), "KJV Romans 8:38 ends mid-sentence");
        check(f("PSA", 90, 12) != nullptr, "KJV includes hour verse Psalm 90:12 beyond chapter 23");
        check(f("GEN", 1, 1) && f("GEN", 1, 1)->text == "In the beginning God created the heaven and the earth.", "KJV Genesis 1:1");
        for (const Verse* v : {f("JHN", 3, 16), f("PSA", 23, 1)}) {
            check(v && v->text.find("\xC2\xB6") == std::string::npos && v->text.find('<') == std::string::npos, "no markup in " + (v ? str(v->ref) : "nil"));
        }
    }

    section("curated");
    {
        const auto& slots = clock.curated().bySlot;
        const auto s316 = slots.find(3 * 60 + 16);
        check(s316 != slots.end() && !s316->second.empty() && s316->second.front() == VerseRef("JHN", 3, 16), "3:16 -> John 3:16 first");
        const auto s413 = slots.find(4 * 60 + 13);
        check(s413 != slots.end() && std::find(s413->second.begin(), s413->second.end(), VerseRef("PHP", 4, 13)) != s413->second.end(), "4:13 includes Philippians 4:13");
        check(clock.curated().hourVerses.size() >= 12, "at least 12 hour verses, got " + std::to_string(clock.curated().hourVerses.size()));
        check(slots.size() > 150, "curated covers " + std::to_string(slots.size()) + " slots");
        for (const auto& t : translations) {
            for (const auto& entry : slots) for (const auto& r : entry.second) check(t->find(r), t->id() + " has curated " + str(r));
            for (const auto& r : clock.curated().hourVerses) check(t->find(r), t->id() + " has hour verse " + str(r));
        }
    }

    section("selection");
    Preferences prefs;
    prefs.hourStyle = HourStyle::TwelveHour;
    const VerseSelector selector(kjv, clock.curated());
    const ReferenceFormatter formatter(canon);
    const auto& curated316 = clock.curated().bySlot.at(3 * 60 + 16);

    const auto threeSixteen = selector.select(at(15, 16, 99), prefs, false);
    check(threeSixteen && threeSixteen->primary().ref == VerseRef("JHN", 3, 16), "15:16 shows John 3:16: " + str(threeSixteen));
    check(threeSixteen && threeSixteen->kind == SelectionKind::Match, "15:16 is a match");
    check(threeSixteen && formatter.string(*threeSixteen) == "John 03:16", "reference padded: " + (threeSixteen ? formatter.string(*threeSixteen) : "nil"));
    check(threeSixteen && ReferenceFormatter(canon, false).string(*threeSixteen) == "John 3:16", "reference unpadded");

    const auto am = selector.select(at(3, 16, 99), prefs, false);
    check(sameRef(am, threeSixteen), "3:16 AM and PM agree");

    int johnDays = 0;
    std::set<std::string> otherFamous;
    for (int day = 0; day < 90; ++day) {
        if (auto s = selector.select(at(3, 16, day), prefs, false)) {
            if (s->primary().ref == VerseRef("JHN", 3, 16)) ++johnDays;
            else otherFamous.insert(str(s->primary().ref));
            check(std::find(curated316.begin(), curated316.end(), s->primary().ref) != curated316.end(), "day " + std::to_string(day) + " 3:16 is curated: " + str(s->primary().ref));
        }
    }
    check(johnDays == 60, "John 3:16 shows two days in three (" + std::to_string(johnDays) + "/90)");
    check(otherFamous.size() == curated316.size() - 1, "every other famous 3:16 verse appears on the remaining days: " + std::to_string(otherFamous.size()));
    check(VerseSelector::famousIndex(1, 5) == 0, "single curated verse always index 0");
    check(VerseSelector::famousIndex(3, -7) >= 0, "negative day ordinals are safe");
    {
        std::vector<int> pattern;
        for (int d = 0; d < 12; ++d) pattern.push_back(VerseSelector::famousIndex(3, d));
        check(pattern == std::vector<int>({0, 0, 1, 0, 0, 2, 0, 0, 1, 0, 0, 2}), "famous index pattern");
        check(VerseSelector::famousIndex(3, -1) == 2 && VerseSelector::famousIndex(3, -4) == 1 && VerseSelector::famousIndex(0, 3) == 0, "famous index for negative days and empty lists");
    }

    Preferences random = prefs;
    random.preferFamous = false;
    const auto a = selector.select(at(3, 16, 100), random, false);
    const auto b = selector.select(at(3, 16, 100), random, false);
    check(a && b && *a == *b, "selection is deterministic for the same day");
    check(a && a->primary().ref.chapter == 3 && a->primary().ref.verse == 16, "random pick still matches 3:16");
    std::set<std::string> differentDays;
    for (int day = 0; day < 30; ++day) {
        if (auto s = selector.select(at(3, 16, day), random, false)) differentDays.insert(str(s->primary().ref));
    }
    check(differentDays.size() > 5, "random mode varies across days (" + std::to_string(differentDays.size()) + " distinct in 30 days)");

    const auto& hourPool = clock.curated().hourVerses;
    const auto top = selector.select(at(9, 0, 100), prefs, false);
    check(top && top->kind == SelectionKind::TopOfHour, "minute 0 shows an hour verse");
    check(top && std::find(hourPool.begin(), hourPool.end(), top->primary().ref) != hourPool.end(), "hour verse comes from the pool");
    std::set<std::string> hours;
    for (int h = 0; h < 24; ++h) {
        if (auto s = selector.select(at(h, 0, 100), prefs, false)) hours.insert(str(s->primary().ref));
    }
    check(hours.size() >= 12, "top-of-hour verses rotate through the day (" + std::to_string(hours.size()) + " distinct)");

    const auto empty = selector.select(at(4, 57, 100), prefs, false);
    check(empty && empty->kind == SelectionKind::Fallback, "4:57 (no chapter 4 verse 57 in the Bible) falls back");
    check(empty && !empty->text().empty(), "fallback has text");

    const auto twentyFour = selector.select(at(15, 16, 100), prefs, true);
    check(twentyFour && twentyFour->primary().ref.chapter == 15 && twentyFour->primary().ref.verse == 16, "24-hour 15:16 prefers chapter 15: " + str(twentyFour));
    const auto lateFold = selector.select(at(23, 58, 100), prefs, true);
    check(lateFold && lateFold->kind == SelectionKind::Fallback, "23:58 has no chapter 23 or 11 verse 58 and falls back: " + str(lateFold));
    const auto fold = selector.select(at(18, 51, 100), prefs, true);
    check(fold && fold->kind == SelectionKind::Match && fold->primary().ref.chapter == 6 && fold->primary().ref.verse == 51, "18:51 folds to chapter 6: " + str(fold));
    const auto excludedOnly = selector.select(at(2, 55, 100), prefs, false);
    check(excludedOnly && excludedOnly->kind == SelectionKind::Fallback, "2:55 (only census verses) falls back to a time verse instead of a list: " + str(excludedOnly));
    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; ++m) {
            if (auto s = selector.select(at(h, m, 7), prefs, false)) {
                check(!(s->primary().flags & FlagExclude), std::to_string(h) + ":" + std::to_string(m) + " never shows an excluded verse: " + str(s->primary().ref));
            }
        }
    }
    const auto midnight = selector.select(at(0, 16, 100), prefs, true);
    check(midnight && midnight->primary().ref.chapter == 12, "00:16 uses chapter 12");

    Preferences extended = prefs;
    extended.extendFragments = true;
    bool sawJude = false;
    for (int day = 0; day < 90; ++day) {
        auto s = selector.select(at(1, 24, day), extended, false);
        if (s && s->primary().ref == VerseRef("JUD", 1, 24)) {
            sawJude = true;
            check(s->isExtended() && s->verses.size() == 2 && s->verses[1]->ref == VerseRef("JUD", 1, 25), "Jude 1:24 is extended to 1:25");
            check(formatter.string(*s) == "Jude 01:24\xE2\x80\x93" "25", "extended reference: " + formatter.string(*s));
            const std::string text = s->text();
            check(text.size() >= 5 && text.compare(text.size() - 5, 5, "Amen.") == 0, "extended text completes the sentence");
            check(text == s->verses[0]->text + " " + s->verses[1]->text, "extended text joins with a space");
            break;
        }
    }
    check(sawJude, "Jude 1:24 appears among the curated 1:24 verses");
    Preferences noExtend = extended;
    noExtend.extendFragments = false;
    for (int day = 0; day < 90; ++day) {
        if (auto s = selector.select(at(1, 24, day), noExtend, false)) check(!s->isExtended(), "no extension when disabled");
    }
    int fragmentsChosen = 0;
    for (int day = 0; day < 60; ++day) {
        if (auto s = selector.select(at(8, 38, day), random, false); s && s->isExtended()) ++fragmentsChosen;
    }
    check(fragmentsChosen > 0, "random mode also uses extendable fragments (" + std::to_string(fragmentsChosen) + "/60 days at 8:38)");
    Preferences randomNoExtend = random;
    randomNoExtend.extendFragments = false;
    for (int day = 0; day < 60; ++day) {
        if (auto s = selector.select(at(8, 38, day), randomNoExtend, false)) {
            check(!s->primary().endsMidSentence() || s->primary().flags == 0, "without extension, fragments are avoided: " + str(s->primary().ref));
        }
    }
    {
        // The 420-character limit and the three-verse cap.
        const Verse* est = kjv.find(VerseRef("EST", 8, 9));
        check(est && selector.extended(*est, true).size() == 1, "Esther 8:9 (the longest verse) is never extended");
        const Verse* rom = kjv.find(VerseRef("ROM", 8, 38));
        const auto chain = rom ? selector.extended(*rom, true) : std::vector<const Verse*>{};
        check(chain.size() == 2 && chain[1]->ref == VerseRef("ROM", 8, 39), "Romans 8:38 extends to 8:39");
        check(rom && selector.extended(*rom, false).size() == 1, "extension disabled keeps one verse");
        for (const Verse* v : kjv.verses(5, 5)) {
            const auto c = selector.extended(*v, true);
            size_t total = 0;
            for (const Verse* x : c) total += characterCount(x->text);
            check(c.size() <= 3 && (c.size() == 1 || total <= 420), "extension bounded for " + str(v->ref));
        }
    }

    section("display variants");
    {
        const ClockTime slot = at(3, 16, 99);
        const auto v0 = selector.select(slot, prefs, false);
        const auto v1 = selector.select(slot, prefs, false, 1);
        const auto v2 = selector.select(slot, prefs, false, 2);
        check(v0 && v0->primary().ref == VerseRef("JHN", 3, 16), "variant 0 is the main display's verse");
        check(v0 && v1 && v2 && v1->primary().ref != v0->primary().ref && v2->primary().ref != v0->primary().ref && v1->primary().ref != v2->primary().ref,
              "three displays show three different 3:16 verses: " + str(v0) + " " + str(v1) + " " + str(v2));
        bool allMatch = true;
        for (const auto* s : {&v0, &v1, &v2}) {
            allMatch = allMatch && *s && (*s)->primary().ref.chapter == 3 && (*s)->primary().ref.verse == 16 && (*s)->kind == SelectionKind::Match;
        }
        check(allMatch, "all variants still match 3:16");
        const auto again = selector.select(slot, prefs, false, 1);
        check(again && v1 && *again == *v1, "variants are deterministic");
        check(sameRef(selector.select(slot, prefs, false, -3), v0), "negative variant is the main display");
        int collisions = 0, total = 0;
        for (int h = 1; h <= 12; ++h) {
            for (int m = 1; m <= 59; ++m) {
                const ClockTime t = at(h, m, 7);
                const auto sa = selector.select(t, random, false);
                if (!sa || sa->kind != SelectionKind::Match) continue;
                const auto sb = selector.select(t, random, false, 1);
                if (!sb) continue;
                ++total;
                size_t pool = 0;
                for (const Verse* v : kjv.verses(h, m)) pool += (v->flags & FlagExclude) ? 0u : 1u;
                if (sa->primary().ref == sb->primary().ref) {
                    ++collisions;
                    check(pool == 1, std::to_string(h) + ":" + std::to_string(m) + " displays coincide only when a single verse exists (pool " + std::to_string(pool) + ")");
                }
            }
        }
        check(total > 600 && collisions < total / 10, "secondary display differs on most minutes (" + std::to_string(collisions) + " coincidences of " + std::to_string(total) + ")");
        const auto top0 = selector.select(at(9, 0, 100), prefs, false);
        const auto top1 = selector.select(at(9, 0, 100), prefs, false, 1);
        check(top0 && top1 && top0->kind == SelectionKind::TopOfHour && top1->kind == SelectionKind::TopOfHour && top0->primary().ref != top1->primary().ref, "top-of-hour verses differ per display");
        try {
            const DisplayContent c1 = clock.content(date(2026, 11, 3, 15, 16), prefs, us, 1);
            check(c1.reference != "John 03:16" && c1.reference.size() >= 5 && c1.reference.compare(c1.reference.size() - 5, 5, "03:16") == 0, "facade passes the variant through: " + c1.reference);
        } catch (const std::exception& e) {
            check(false, std::string("display variants threw ") + e.what());
        }
    }

    section("coverage");
    for (const auto& t : translations) {
        const VerseSelector sel(*t, clock.curated());
        for (const bool use24 : {false, true}) {
            int missing = 0, matched = 0, topOfHour = 0, fallback = 0;
            for (int h = 0; h < 24; ++h) {
                for (int m = 0; m < 60; ++m) {
                    const auto s = sel.select(at(h, m, 42), prefs, use24);
                    if (!s) {
                        ++missing;
                        continue;
                    }
                    if (s->kind == SelectionKind::Match) ++matched;
                    else if (s->kind == SelectionKind::TopOfHour) ++topOfHour;
                    else ++fallback;
                    if (s->text().empty()) ++missing;
                    if (s->kind == SelectionKind::Match) {
                        const auto chapters = VerseSelector::chapters(at(h, m, 42), use24);
                        check(std::find(chapters.begin(), chapters.end(), s->primary().ref.chapter) != chapters.end() && s->primary().ref.verse == m,
                              t->id() + " " + std::to_string(h) + ":" + std::to_string(m) + " match is " + str(s->primary().ref));
                    }
                }
            }
            const std::string label = t->id() + (use24 ? " 24h" : " 12h");
            check(missing == 0, label + ": every minute has text (missing " + std::to_string(missing) + ")");
            if (!use24) {
                std::vector<std::string> fallbackSlots;
                for (int h = 1; h <= 12; ++h) {
                    for (int m = 1; m <= 59; ++m) {
                        const auto s = sel.select(at(h, m, 42), prefs, false);
                        if (s && s->kind == SelectionKind::Fallback) {
                            char buffer[32];
                            std::snprintf(buffer, sizeof buffer, "%d:%02d", h, m);
                            fallbackSlots.push_back(buffer);
                        }
                    }
                }
                std::printf("   %s 12h slots without a verse (%zu): %s\n", t->id().c_str(), fallbackSlots.size(), join(fallbackSlots, " ").c_str());
            }
            check(matched >= 1300, label + ": " + std::to_string(matched) + " matched minutes, " + std::to_string(topOfHour) + " top-of-hour, " + std::to_string(fallback) + " fallback");
        }
    }

    section("verse clock facade");
    prefs.hourStyle = HourStyle::TwelveHour;
    try {
        const DisplayContent content = clock.content(date(2026, 11, 3, 15, 16), prefs, us);
        check(content.reference == "John 03:16", "facade reference: " + content.reference);
        check(content.clock == "3:16 PM", "12h clock string: " + content.clock);
        check(content.translation == "KJV", "translation abbreviation");
        check(content.kind == SelectionKind::Match && content.minuteKey == 15 * 60 + 16, "facade kind and minute key");
        prefs.hourStyle = HourStyle::TwentyFourHour;
        const DisplayContent c24 = clock.content(date(2026, 11, 3, 15, 16), prefs, us);
        check(c24.clock == "15:16", "24h clock string: " + c24.clock);
        check(c24.reference.size() >= 5 && c24.reference.compare(c24.reference.size() - 5, 5, "15:16") == 0, "24h reference: " + c24.reference);
        prefs.hourStyle = HourStyle::TwelveHour;
        prefs.translation = "dby";
        const DisplayContent darby = clock.content(date(2026, 11, 3, 15, 16), prefs, us);
        check(darby.translation == "DBY" && darby.reference == "John 03:16" && darby.text.find("whosoever believes on him") != std::string::npos, "Darby wording: " + darby.reference + " " + darby.text);
        prefs.translation = "nope";
        const DisplayContent fallback = clock.content(date(2026, 11, 3, 15, 16), prefs, us);
        check(fallback.translation == "KJV", "unknown translation falls back to first available");
        prefs.translation = "kjv";
        const auto specific = clock.content(VerseRef("EST", 8, 9), date(2026, 11, 3, 8, 9), prefs, us);
        check(specific && specific->reference == "Esther 08:09" && specific->clock == "8:09 AM" && specific->kind == SelectionKind::Match, "content for a reference: " + (specific ? specific->reference + " " + specific->clock : "nil"));
        check(!clock.content(VerseRef("JHN", 99, 99), date(2026, 11, 3, 8, 9), prefs, us), "content for a missing reference is nil");
        const auto romans = clock.content(VerseRef("ROM", 8, 38), date(2026, 11, 3, 8, 38), prefs, us);
        check(romans && romans->reference == "Romans 08:38\xE2\x80\x93" "39", "content for a reference extends fragments: " + (romans ? romans->reference : "nil"));
        Preferences systemHour = prefs;
        systemHour.hourStyle = HourStyle::System;
        check(clock.content(date(2026, 11, 3, 15, 16), systemHour, de).clock == "15:16", "system hour style follows the locale (24h)");
        check(clock.content(date(2026, 11, 3, 15, 16), systemHour, us).clock == "3:16 PM", "system hour style follows the locale (12h)");
    } catch (const std::exception& e) {
        check(false, std::string("facade threw ") + e.what());
    }

    section("clock string");
    check(clockString(0, 5, false, us) == "12:05 AM", "midnight in 12h: " + clockString(0, 5, false, us));
    check(clockString(12, 0, false, us) == "12:00 PM", "noon in 12h");
    check(clockString(23, 59, false, us) == "11:59 PM", "23:59 in 12h");
    check(clockString(0, 5, true, us) == "00:05" && clockString(9, 7, true, us) == "09:07", "24h is zero padded");
    check(clockString(15, 16, false, LocaleInfo{false, "vorm.", "nachm."}) == "3:16 nachm.", "locale symbols are used");

    section("preferences");
    {
        const Preferences defaults;
        check(Preferences::fromJSON(defaults.toJSON()) == defaults, "dictionary round-trip");
        check(Preferences::fromJSONText(R"({"textScale": 9.0})").textScale == Preferences::textScaleMax, "textScale clamped high");
        check(Preferences::fromJSONText(R"({"textScale": 0.1})").textScale == Preferences::textScaleMin, "textScale clamped low");
        check(Preferences::fromJSONText(R"({"textScale": 2})").textScale == 2.0, "integer textScale accepted");
        check(Preferences::fromJSONText(R"({"textScale": "1.5"})").textScale == 1.0, "string textScale ignored");
        check(Preferences::fromJSONText(R"({"layout": "bogus", "theme": "night"})").layout == LayoutStyle::Clock, "bad enum keeps default");
        check(Preferences::fromJSONText(R"({"layout": "bogus", "theme": "night"})").theme == ThemeChoice::Night, "good enum applies");
        check(Preferences().displayMode == DisplayMode::SameVerse, "default display mode shows the same verse everywhere");
        check(Preferences::fromJSONText(R"({"displayMode": "differentVerses"})").displayMode == DisplayMode::DifferentVerses, "display mode decodes");
        check(Preferences::fromJSONText(R"({"displayMode": "nope"})").displayMode == DisplayMode::SameVerse, "bad display mode keeps default");
        check(Preferences::fromJSONText(R"({"showClock": 2, "preferFamous": "no", "extendFragments": 0.5, "padReference": false})") == [] { Preferences p; p.padReference = false; return p; }(), "only booleans and the numbers 0/1 apply");
        // JSONSerialization numbers bridge to Bool when they equal 0 or 1 (NSNumber `as? Bool`).
        {
            const Preferences numeric = Preferences::fromJSONText(R"({"showClock": 0, "preferFamous": 0.0, "extendFragments": -0, "padReference": 1e0})");
            check(!numeric.showClock && !numeric.preferFamous && !numeric.extendFragments && numeric.padReference, "0, 0.0, -0 are false; 1e0 is true");
            const Preferences ones = Preferences::fromJSONText(R"({"showClock": 1, "preferFamous": 1.0, "extendFragments": -1, "padReference": 1.5})");
            check(ones.showClock && ones.preferFamous && ones.extendFragments && ones.padReference, "1 and 1.0 are true; -1 and 1.5 keep the default");
            const Preferences off = Preferences::fromJSONText(R"({"showClock": 1, "preferFamous": true, "extendFragments": false, "padReference": 0})");
            check(off.showClock && off.preferFamous && !off.extendFragments && !off.padReference, "booleans and 0/1 mixed");
        }
        // `as? NSNumber` also accepts JSON booleans for textScale: true is 1.0, false is 0.0 (clamped to the minimum).
        check(Preferences::fromJSONText(R"({"textScale": true})").textScale == 1.0, "textScale true is 1.0");
        check(Preferences::fromJSONText(R"({"textScale": false})").textScale == Preferences::textScaleMin, "textScale false is 0.0, clamped");
        check(Preferences::fromJSONText(R"({"textScale": 1.25})").textScale == 1.25, "fractional textScale");
        check(Preferences::fromJSONText(R"({"textScale": null})").textScale == 1.0, "null textScale ignored");
        check(Preferences::fromJSONText(R"({"translation": ""})").translation == "kjv" && Preferences::fromJSONText(R"({"translation": "dby"})").translation == "dby", "empty translation keeps the default");
        check(Preferences::fromJSONText("not json") == defaults && Preferences::fromJSONText("[1,2]") == defaults, "malformed JSON yields defaults");
        Preferences p;
        p.hourStyle = HourStyle::TwentyFourHour;
        check(p.uses24Hour(false), "explicit 24h overrides locale");
        p.hourStyle = HourStyle::TwelveHour;
        check(!p.uses24Hour(true), "explicit 12h overrides locale");
        p.hourStyle = HourStyle::System;
        check(p.uses24Hour(true) && !p.uses24Hour(false), "system follows the locale");
        Preferences custom;
        custom.translation = "dby";
        custom.hourStyle = HourStyle::TwentyFourHour;
        custom.theme = ThemeChoice::Night;
        custom.layout = LayoutStyle::Book;
        custom.typeface = TypefaceChoice::Georgia;
        custom.textScale = 1.35;
        custom.showClock = false;
        custom.preferFamous = false;
        custom.extendFragments = false;
        custom.padReference = false;
        custom.displayMode = DisplayMode::DifferentVerses;
        check(Preferences::fromJSONText(custom.toJSONText()) == custom, "JSON text round-trip: " + custom.toJSONText());
        check(Preferences::fromJSONText(defaults.toJSONText()) == defaults, "Codable-style round-trip of defaults");
        const json::Value j = custom.toJSON();
        check(j.find("typeface") && j.find("typeface")->asString() == "georgia" && j.find("hourStyle")->asString() == "twentyFourHour" && j.find("displayMode")->asString() == "differentVerses", "raw values match Swift");
        check(j.members().size() == 11 && j.members().front().first == "translation" && j.members().back().first == "displayMode", "all eleven keys in Swift order");
        for (const auto e : allCases<TypefaceChoice>()) check(typefaceFrom(rawValue(e)) == e, std::string("typeface raw value round-trips: ") + rawValue(e));
        for (const auto e : allCases<HourStyle>()) check(hourStyleFrom(rawValue(e)) == e, std::string("hour style raw value round-trips: ") + rawValue(e));
        for (const auto e : allCases<ThemeChoice>()) check(themeFrom(rawValue(e)) == e, std::string("theme raw value round-trips: ") + rawValue(e));
        for (const auto e : allCases<LayoutStyle>()) check(layoutFrom(rawValue(e)) == e, std::string("layout raw value round-trips: ") + rawValue(e));
        for (const auto e : allCases<DisplayMode>()) check(displayModeFrom(rawValue(e)) == e, std::string("display mode raw value round-trips: ") + rawValue(e));
        check(!layoutFrom("Clock") && !themeFrom(""), "raw values are case-sensitive");
        check(std::string(title(DisplayMode::MainOnly)) == "Main display only" && std::string(title(HourStyle::System)) == "Follow System" && std::string(title(ThemeChoice::Auto)) == "Automatic", "titles");
    }

    section("json");
    {
        const json::Value v = json::parse(R"({"s":"a\"b\\c\/d\b\f\n\r\t","u":"\u00e9\u2013\ud83d\ude00","n":[0,-7,3.5,1e2,-2.5E-3,12345678901234567890,9007199254740993],"t":true,"f":false,"z":null,"o":{}})");
        check(v.isObject() && v.members().size() == 7, "object with seven members");
        check(v.find("s") && v.find("s")->asString() == "a\"b\\c/d\b\f\n\r\t", "simple escapes");
        check(v.find("u") && v.find("u")->asString() == "\xC3\xA9\xE2\x80\x93\xF0\x9F\x98\x80", "\\u escapes and a surrogate pair decode to UTF-8");
        const auto& n = v.find("n")->items();
        check(n.size() == 7, "seven numbers");
        check(n[0].isInteger() && n[0].asInt() == 0 && n[1].isInteger() && n[1].asInt() == -7, "integers");
        check(!n[2].isInteger() && n[2].asDouble() == 3.5, "fraction");
        check(n[3].isInteger() && n[3].asInt() == 100 && n[3].asDouble() == 100.0, "1e2 is integral");
        check(n[4].asDouble() == -2.5e-3, "negative exponent");
        check(!n[5].isInteger() && n[5].asDouble() == 12345678901234567890.0, "beyond int64 is a double");
        check(n[6].isInteger() && n[6].asInt() == 9007199254740993LL, "int64 kept exactly beyond 2^53");
        check(v.find("t")->asBool() && !v.find("f")->asBool() && v.find("z")->isNull() && v.find("o")->isObject(), "literals");
        check(!v.find("missing"), "find returns null for missing keys");
        check(json::parse("\xEF\xBB\xBF [1]").items().size() == 1, "byte order mark skipped");
        check(json::parse("0.1").asDouble() == 0.1 && json::parse("123.456e-2").asDouble() == 1.23456 && json::parse("1.7976931348623157e308").asDouble() == 1.7976931348623157e308, "doubles round to nearest");
        check(json::parse("-9223372036854775808").asInt() == INT64_MIN && json::parse("9223372036854775807").asInt() == INT64_MAX, "int64 limits");
        check(json::parse("\"\xE2\x80\x94\"").asString() == "\xE2\x80\x94", "raw UTF-8 passes through");
        for (const char* bad : {"", "{", "[1,]", "{\"a\":1,}", "[1 2]", "01", "1.", ".5", "-", "1e", "+1", "\"abc", "\"\\x\"", "\"\\ud800\"", "\"\\udc00x\"",
                                "\"\\ud83d\\u0041\"", "\"a\nb\"", "tru", "nul", "[1] x", "{1:2}", "\"\xC0\xAF\"", "\"\xED\xA0\x80\"", "\"\xF5\x80\x80\x80\"", "\"\xE2\x80\"", "1e999"}) {
            check(throwsParseError([&] { json::parse(bad); }), std::string("rejects malformed JSON: ") + bad);
        }
        std::string deep(600, '[');
        check(throwsParseError([&] { json::parse(deep); }), "rejects absurd nesting instead of overflowing the stack");
        const json::Value round = json::parse(json::serialize(v));
        check(json::serialize(round) == json::serialize(v), "serialize/parse round trip");
        check(json::serialize(json::Value::makeString("q\"\\\n\x01\xC3\xA9")) == "\"q\\\"\\\\\\n\\u0001\xC3\xA9\"", "string escaping");
        check(json::serialize(json::Value::makeNumber(1.35)) == "1.35" && json::serialize(json::Value::makeNumber(0.1)) == "0.1" && json::serialize(json::Value::makeInt(-3)) == "-3", "number formatting");
        check(json::serialize(json::Value::makeNumber(2.0)) == "2", "integral doubles print as integers");
        json::Value obj = json::Value::makeObject();
        obj.set("a", json::Value::makeInt(1));
        obj.set("b", json::Value::makeBool(true));
        obj.set("a", json::Value::makeInt(2));
        check(json::serialize(obj) == "{\"a\":2,\"b\":true}", "set replaces and appends: " + json::serialize(obj));
        check(json::serialize(obj, 2) == "{\n  \"a\": 2,\n  \"b\": true\n}", "pretty printing");
        check(json::serialize(json::Value::makeArray()) == "[]" && json::serialize(json::Value::makeObject(), 2) == "{}", "empty containers");
    }

    section("character count");
    check(characterCount("") == 0 && characterCount("Jesus wept.") == 11, "ASCII");
    check(characterCount("\xE2\x80\x94\xE2\x80\x99\xC3\xA6") == 3, "precomposed scalars count once");
    check(characterCount("e\xCC\x81") == 1, "combining acute joins its base");
    check(characterCount("\xD7\x91\xD6\xBC\xD6\xB0") == 1, "Hebrew letter with points is one character");
    check(characterCount("a\r\nb") == 3, "CR LF counts once");
    check(characterCount("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == 1, "emoji with a skin tone modifier");
    check(characterCount("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9") == 1, "ZWJ sequence");
    check(characterCount("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAC\xF0\x9F\x87\xA7") == 2, "two flags");
    check(characterCount("\xE2\x9D\xA4\xEF\xB8\x8F") == 1, "variation selector joins");
    // Imported translations in other scripts: String.count is UAX #29 extended grapheme clusters (Unicode 15.1).
    check(characterCount("தேவன் உலகத்தில் அன்புகூர்ந்தார்,") == 20, "Tamil: " + std::to_string(characterCount("தேவன் உலகத்தில் அன்புகூர்ந்தார்,")));
    check(characterCount("क्षत्रिय") == 3, "Devanagari conjuncts join (GB9c)");
    check(characterCount("हिन्दी") == 2, "Hindi with a virama conjunct");
    check(characterCount("พระเจ้า") == 6, "Thai: SARA A and SARA E stand alone, MAI THO joins");
    check(characterCount("ำ") == 1 && characterCount("กำ") == 1, "Thai SARA AM is a SpacingMark");
    check(characterCount("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8") == 1, "Hangul jamo L V T form one syllable");
    check(characterCount("\xEA\xB0\x80\xE1\x86\xA8") == 1 && characterCount("\xEA\xB0\x81\xE1\x85\xA1") == 2, "LV + T joins, LVT + V does not");
    check(characterCount("하나님이 세상을") == 8, "precomposed Hangul");
    check(characterCount("👨‍👩‍👧‍👦") == 1, "family ZWJ sequence");
    check(characterCount("🏳️‍🌈") == 1, "rainbow flag (VS16 + ZWJ)");
    check(characterCount("a\xE2\x80\x8D👍") == 2, "ZWJ after a letter joins the letter only");
    check(characterCount("🇺🇸🇬") == 2, "odd regional indicator stands alone");
    check(characterCount("🏴󠁧󠁢󠁳󠁣󠁴󠁿") == 1, "tag sequence flag");
    check(characterCount("\xD8\xA8\xD9\x90\xD8\xB3\xD9\x92\xD9\x85\xD9\x90") == 3, "Arabic with harakat");
    check(characterCount("\xD8\x80\xD9\xA1") == 1, "Arabic number sign is Prepend");
    check(characterCount("בְּרֵאשִׁית") == 6, "Hebrew with points");
    check(characterCount("\r\n\r\n") == 2 && characterCount("\n\r") == 2 && characterCount("\r\xCC\x81") == 2, "CR LF, LF CR, CR + mark");
    check(characterCount("\x01\xCC\x81") == 2, "controls do not take marks");
    {
        // Every case of the Unicode 15.1 GraphemeBreakTest.txt (generated into GraphemeBreakTest.inc).
        struct GraphemeCase {
            size_t count;
            uint32_t scalars[8];
            size_t starts[8];
        };
#include "GraphemeBreakTest.inc"
        size_t passed = 0;
        std::string firstFailure;
        for (const GraphemeCase& c : kGraphemeBreakCases) {
            unicode::GraphemeBreaker breaker;
            std::vector<size_t> starts;
            std::string utf8;
            for (size_t k = 0; k < c.count; ++k) {
                if (breaker.startsCluster(c.scalars[k])) starts.push_back(k);
                unicode::appendUTF8(utf8, c.scalars[k]);
            }
            std::vector<size_t> expected;
            for (size_t k = 0; k < 8 && (k == 0 || c.starts[k] != 0); ++k) expected.push_back(c.starts[k]);
            if (starts == expected && characterCount(utf8) == expected.size()) ++passed;
            else if (firstFailure.empty()) {
                for (size_t k = 0; k < c.count; ++k) firstFailure += (k ? " " : "") + std::to_string(c.scalars[k]);
            }
        }
        const size_t total = sizeof(kGraphemeBreakCases) / sizeof(kGraphemeBreakCases[0]);
        check(total > 1000 && passed == total, "GraphemeBreakTest.txt: " + std::to_string(passed) + "/" + std::to_string(total) + " pass; first failure: " + firstFailure);
    }
    {
        Verse v;
        v.text = "and,";
        check(v.endsMidSentence(), "comma ends mid-sentence");
        v.text = "and\xE2\x80\x94";
        check(v.endsMidSentence(), "em dash ends mid-sentence");
        v.text = "and\xE2\x80\x93";
        check(v.endsMidSentence(), "en dash ends mid-sentence");
        v.text = "and-";
        check(v.endsMidSentence(), "hyphen ends mid-sentence");
        v.text = "and.";
        check(!v.endsMidSentence(), "period ends the sentence");
        v.text = "said,\xE2\x80\x99";
        check(!v.endsMidSentence(), "closing quote after a comma is not mid-sentence (last scalar only)");
        v.text = "";
        check(!v.endsMidSentence(), "empty text");
    }

    section("day ordinal");
    // Foundation's values (see dayOrdinalFor): Julian day number - 1721425 (UTC days), Julian calendar before 1582-10-15.
    check(ClockTime::dayOrdinalFor(1970, 1, 1) == 719163, "1970-01-01 is 719163");
    check(ClockTime::dayOrdinalFor(2026, 11, 3) == 739923, "2026-11-03 is 739923");
    check(ClockTime::dayOrdinalFor(2026, 11, 5) == 739925, "2026-11-05 is 739925");
    check(ClockTime::dayOrdinalFor(2000, 2, 29) == 730179, "2000-02-29 is 730179");
    check(ClockTime::dayOrdinalFor(2000, 3, 1) == 730180, "2000-03-01 follows the leap day");
    check(ClockTime::dayOrdinalFor(1900, 3, 1) - ClockTime::dayOrdinalFor(1900, 2, 28) == 1, "1900 is not a leap year");
    check(ClockTime::dayOrdinalFor(2200, 12, 31) == 803533, "2200-12-31 is 803533");
    check(ClockTime::dayOrdinalFor(1582, 10, 15) == 577736, "Gregorian reform day 1582-10-15 is 577736");
    check(ClockTime::dayOrdinalFor(1582, 10, 4) == 577735, "1582-10-04 (Julian) is the day before the reform");
    check(ClockTime::dayOrdinalFor(1, 1, 1) == -1, "0001-01-01 (Julian) is -1");
    check(ClockTime::dayOrdinalForUnixTime(0) == 719163, "the Unix epoch is 719163");
    check(ClockTime::dayOrdinalForUnixTime(86399) == 719163 && ClockTime::dayOrdinalForUnixTime(86400) == 719164, "turns over at 00:00 UTC");
    check(ClockTime::dayOrdinalForUnixTime(-1) == 719162, "one second before the epoch is the previous day");
    check(ClockTime::dayOrdinalForUnixTime(1793664000) == ClockTime::dayOrdinalFor(2026, 11, 3), "2026-11-03T00:00Z");
    check(ClockTime::dayOrdinalFor(1500, 3, 1) - ClockTime::dayOrdinalFor(1500, 2, 28) == 2, "1500 is a Julian leap year");
    {
        bool consecutive = true;
        int64_t previous = ClockTime::dayOrdinalFor(1999, 12, 31);
        const int lengths[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        for (int m = 1; m <= 12; ++m) {
            for (int d = 1; d <= lengths[m - 1]; ++d) {
                const int64_t o = ClockTime::dayOrdinalFor(2000, m, d);
                consecutive = consecutive && o == previous + 1;
                previous = o;
            }
        }
        check(consecutive, "every day of 2000 is one more than the day before");
    }
    check(at(0, 0, 0).hour12() == 12 && at(12, 0, 0).hour12() == 12 && at(13, 0, 0).hour12() == 1 && at(23, 59, 0).minuteKey() == 1439, "hour12 and minuteKey");

    section("seeded generator");
    {
        SeededGenerator g1(at(3, 16, 7), "kjv"), g2(at(3, 16, 7), "kjv"), g3(at(3, 16, 8), "kjv");
        check(g1.next() == g2.next(), "same seed, same sequence");
        check(g1.next() != g3.next(), "different day, different sequence");
        SeededGenerator zero(0);
        const uint64_t z1 = zero.next(), z2 = zero.next(), z3 = zero.next();
        check(z1 == 0xE220A8397B1DCDAFull && z2 == 0x6E789E6AA1B965F4ull && z3 == 0x06C45D188009454Full, "SplitMix64 reference outputs for seed 0");
        // Expected values computed independently (Python big integers) from Swift's next(upperBound:) algorithm.
        const auto sequence = [](uint64_t seed, uint64_t bound) {
            SeededGenerator g(seed);
            std::vector<uint64_t> out;
            for (int i = 0; i < 5; ++i) out.push_back(g.next(bound));
            return out;
        };
        check(sequence(0, 3) == std::vector<uint64_t>({2, 1, 0, 2, 0}), "next(upperBound: 3)");
        check(sequence(1, 10) == std::vector<uint64_t>({5, 7, 9, 4, 4}), "next(upperBound: 10)");
        check(sequence(42, 1000003) == std::vector<uint64_t>({741567, 159910, 278601, 344191, 38030}), "next(upperBound: 1000003)");
        check(sequence(12345, 6) == std::vector<uint64_t>({0, 1, 0, 1, 3}), "next(upperBound: 6)");
        check(sequence(7, (uint64_t{1} << 63) + 1) == std::vector<uint64_t>({3595544800446187243ull, 8308050873407804673ull, 2300599727732774152ull, 1238314238945538992ull, 3810556812210252212ull}),
              "next(upperBound: 2^63 + 1) exercises rejection");
        check(sequence(7, UINT64_MAX) == std::vector<uint64_t>({7191089600892374486ull, 309689372594955803ull, 16616101746815609345ull, 10753165928301472202ull, 8346079845500723673ull}),
              "next(upperBound: UInt64.max)");
        SeededGenerator one(99), raw(99);
        raw.next();
        const uint64_t second = raw.next();
        check(one.next(1) == 0, "upperBound 1 is always 0");
        check(one.next() == second, "upperBound 1 consumes exactly one value");
        SeededGenerator pow2(5), raw2(5);
        check(pow2.next(1 << 10) == raw2.next() >> 54, "a power-of-two bound takes the top bits");
        SeededGenerator salted(at(3, 16, 7), "kjv#1"), unsalted(at(3, 16, 7), "kjv");
        check(salted.next() != unsalted.next(), "salt changes the sequence");
    }

    section("translation ids");
    // Swift: id.trimmingCharacters(in: .whitespaces).lowercased().
    check(TranslationHeader::normalizeID(" \tKJV  ") == "kjv", "ASCII trim and lowercase");
    check(TranslationHeader::normalizeID("\xC2\xA0\xE3\x80\x80My Bible\xE2\x80\xAF\xE2\x81\x9F\xE1\x9A\x80\xE2\x80\x80\xE2\x80\x8A") == "my bible", "Zs whitespace trimmed at both ends, inner kept");
    check(TranslationHeader::normalizeID("kjv\n") == "kjv\n" && TranslationHeader::normalizeID("\xE2\x80\x8Bkjv") == "\xE2\x80\x8Bkjv", "newlines and ZWSP are not .whitespaces");
    check(TranslationHeader::normalizeID("ÀÉÎÕÜÇÑÞ") == "àéîõüçñþ", "Latin-1");
    check(TranslationHeader::normalizeID("ĀĞŁŐŸŽǄǅǱ") == "āğłőÿžǆǆǳ", "Latin Extended-A/B, title-case digraphs");
    check(TranslationHeader::normalizeID("ΑΒΓΆΏ") == "αβγάώ", "Greek");
    check(TranslationHeader::normalizeID("БИБЛИЯ ЁЇЄ") == "библия ёїє", "Cyrillic");
    check(TranslationHeader::normalizeID("ԱՍՏՎԱԾԱՇՈՒՆՉ") == "աստվածաշունչ", "Armenian");
    check(TranslationHeader::normalizeID("ႠႡ") == "ⴀⴁ" && TranslationHeader::normalizeID("ᲐᲑ") == "აბ", "Georgian");
    check(TranslationHeader::normalizeID("İ") == "i\xCC\x87", "U+0130 lowercases to i + combining dot (SpecialCasing)");
    check(TranslationHeader::normalizeID("ẞ𐐀Ⅻ") == "ß𐐨ⅻ", "capital sharp s, Deseret, Roman numerals");
    check(TranslationHeader::normalizeID("ß中文ﬀ") == "ß中文ﬀ", "already lowercase or uncased");
    // Swift: every Character isLetter || isNumber || "-" || "_" (first scalar's Alphabetic / Numeric_Type).
    check(TranslationImporter::isValidID("kjv") && TranslationImporter::isValidID("my-bible_2"), "ASCII ids");
    check(TranslationImporter::isValidID("библия") && TranslationImporter::isValidID("中文和合本") && TranslationImporter::isValidID("ελληνικά"), "Cyrillic, Han, Greek letters");
    check(TranslationImporter::isValidID("பைபிள்") && TranslationImporter::isValidID("हिन्दी") && TranslationImporter::isValidID("성경"), "Tamil, Hindi, Hangul clusters");
    check(TranslationImporter::isValidID("e\xCC\x81") && TranslationImporter::isValidID("ि"), "letter + mark; vowel sign alone is Alphabetic");
    check(TranslationImporter::isValidID("½²٣Ⅻ"), "No / Nd / Nl numbers");
    check(!TranslationImporter::isValidID("") && !TranslationImporter::isValidID("a b") && !TranslationImporter::isValidID("a.b"), "empty, space, period");
    check(!TranslationImporter::isValidID("bible😀") && !TranslationImporter::isValidID("a\xC2\xA0" "b"), "emoji and NBSP");
    check(!TranslationImporter::isValidID("\xCC\x81" "a") && !TranslationImporter::isValidID("-\xCC\x81"), "leading mark; \"-\" + mark is not \"-\"");
    {
        // Imported translations sort with compareNames (stable); the default is Unicode case-insensitive.
        check(compareNames("alpha", "ALPHA") == 0 && compareNames("Äpfel", "äPFEL") == 0 && compareNames("beta", "Alpha") > 0, "default comparator");
        check(unicode::caseInsensitiveCompare("ΒΊΒΛΟΣ", "βίβλοσ") == 0 && unicode::caseInsensitiveCompare("a", "b") < 0, "Greek case-insensitive equality");
        const fs::path sortRoot = fs::temp_directory_path() / ("BibleTimeSort-" + std::to_string(BT_GETPID()));
        std::error_code sortEC;
        fs::remove_all(sortRoot, sortEC);
        fs::create_directories(sortRoot);
        const auto header = [&](const char* file, const char* id, const char* name) {
            writeText(sortRoot / file, std::string("{\"id\":\"") + id + "\",\"name\":\"" + name + "\",\"abbreviation\":\"X\",\"verses\":[]}");
        };
        header("a.json", "zeta", "zeta");
        header("b.json", "same2", "SAME");
        header("c.json", "eclair", "Éclair");
        header("d.json", "alpha", "Alpha");
        header("e.json", "same1", "same");
        header("f.json", "beta", "beta");
        VerseClock sorting(directoryLoader(resources), {sortRoot});
        check(ids(sorting.catalog().available()) == std::vector<std::string>({"kjv", "dby", "alpha", "beta", "same2", "same1", "zeta", "eclair"}),
              "imported sorted case-insensitively, ties keep file order: " + join(ids(sorting.catalog().available())));
        setNameComparator([](const std::string& a, const std::string& b) { return unicode::caseInsensitiveCompare(b, a); });
        sorting.catalog().rescan();
        check(ids(sorting.catalog().available()) == std::vector<std::string>({"kjv", "dby", "eclair", "zeta", "same2", "same1", "beta", "alpha"}),
              "installed comparator decides the order: " + join(ids(sorting.catalog().available())));
        setNameComparator(nullptr);
        sorting.catalog().rescan();
        check(ids(sorting.catalog().available()).back() == "eclair", "empty comparator restores the default");
        fs::remove_all(sortRoot, sortEC);
    }

    section("import");
    const fs::path importRoot = fs::temp_directory_path() / ("BibleTimeTests-" + std::to_string(BT_GETPID()));
    const fs::path importDir = importRoot / "Translations";
    const fs::path dropDir = importRoot / "Manual";
    std::error_code ec;
    fs::remove_all(importRoot, ec);
    fs::create_directories(dropDir);

    const auto kjvText = text::readFile(resources / "Translations" / "kjv.json");
    check(kjvText.has_value(), "read kjv.json");
    const json::Value kjvJSON = kjvText ? json::parse(*kjvText) : json::Value::makeObject();
    const auto fakeTranslation = [&](const std::string& id, const std::string& name = "Test Version", const std::string& abbreviation = "TEST",
                                     const std::function<void(json::Value&)>& mutate = nullptr) {
        json::Value object = kjvJSON;
        object.set("id", json::Value::makeString(id));
        object.set("name", json::Value::makeString(name));
        object.set("abbreviation", json::Value::makeString(abbreviation));
        if (mutate) mutate(object);
        return json::serialize(object);
    };
    const auto versesOf = [](json::Value& object) -> std::vector<json::Value>& {
        for (auto& member : object.members()) {
            if (member.first == "verses") return member.second.items();
        }
        throw std::runtime_error("no verses");
    };

    try {
        VerseClock importing(directoryLoader(resources), {importDir, dropDir});
        check(ids(importing.catalog().available()) == std::vector<std::string>({"kjv", "dby"}), "no imports yet");
        const auto importerOpt = importing.importer();
        check(importerOpt.has_value(), "importer available");
        if (!importerOpt) return 1;
        const TranslationImporter& importer = *importerOpt;
        check(importer.destination() == importDir, "imports go to the first directory");

        const fs::path source = importRoot / "my-translation.json";
        writeText(source, fakeTranslation("Test-1"));
        const TranslationInfo info = importer.importFile(source);
        check(info.id == "test-1" && info.isImported && info.path == importDir / "test-1.json", "imported as test-1.json: " + info.path.u8string());
        check(!fs::exists(importDir / ".test-1.json.part"), "temporary file renamed away");
        importing.catalog().rescan();
        check(ids(importing.catalog().available()) == std::vector<std::string>({"kjv", "dby", "test-1"}), "catalog lists the import: " + join(ids(importing.catalog().available())));
        check(importing.catalog().info("test-1") && importing.catalog().info("test-1")->displayName() == "Test Version (TEST)", "display name");
        Preferences p;
        p.translation = "test-1";
        p.hourStyle = HourStyle::TwelveHour;
        const DisplayContent content = importing.content(date(2026, 11, 3, 15, 16), p, us);
        check(content.translation == "TEST" && content.reference == "John 03:16", "imported translation is used: " + content.translation + " " + content.reference);
        const TranslationHeader header = TranslationHeader::fromFile(info.path);
        check(header == (TranslationHeader{"test-1", "Test Version", "TEST"}), "header read from the file head: " + header.id + " " + header.name);
        // Header in an unusual key order (after the verses) takes the full-parse path.
        {
            json::Value reordered = json::Value::makeObject();
            json::Value copy = kjvJSON;
            reordered.set("verses", json::Value::makeArray(versesOf(copy)));
            reordered.set("name", json::Value::makeString("Late Header"));
            reordered.set("abbreviation", json::Value::makeString("LATE"));
            reordered.set("id", json::Value::makeString("  Late "));
            const fs::path late = importRoot / "late.json";
            writeText(late, json::serialize(reordered));
            check(TranslationHeader::fromFile(late) == (TranslationHeader{"late", "Late Header", "LATE"}), "header after the verses falls back to a full parse");
            writeText(late, "{\"id\":\"x\",\"verses\":[]}");
            bool threw = false;
            try {
                TranslationHeader::fromFile(late);
            } catch (const ImportError& e) {
                threw = e.kind == ImportError::Kind::InvalidFormat;
            }
            check(threw, "header without name is invalidFormat");
            fs::remove(late);
        }

        // Manual drop directory and precedence of the first directory for duplicate ids.
        writeText(dropDir / "anything.json", fakeTranslation("dropped", "Dropped", "DRP"));
        writeText(dropDir / "shadow.json", fakeTranslation("test-1", "Shadowed"));
        writeText(dropDir / "notes.txt", "not a translation");
        writeText(dropDir / "broken.json", "{\"id\": ");
        importing.catalog().rescan();
        check(ids(importing.catalog().available()) == std::vector<std::string>({"kjv", "dby", "dropped", "test-1"}),
              "manual drop folder is scanned and the first directory wins: " + join(ids(importing.catalog().available())));
        check(importing.catalog().info("test-1") && importing.catalog().info("test-1")->name == "Test Version", "duplicate id in a later directory is ignored");

        // Re-import replaces in place.
        writeText(source, fakeTranslation("test-1", "Test Version 2"));
        importer.importFile(source);
        importing.catalog().rescan();
        check(importing.catalog().info("test-1") && importing.catalog().info("test-1")->name == "Test Version 2", "re-import replaces the file");
        const auto reloaded = importing.catalog().translation("test-1");
        check(reloaded && reloaded->name() == "Test Version 2", "cache is invalidated on rescan");

        // Rejections.
        const auto rejects = [&](const std::string& data, const std::string& label, ImportError::Kind kind, const std::vector<std::string>& codes = {}) {
            try {
                importer.validate(data);
                check(false, label + " should be rejected");
            } catch (const ImportError& e) {
                check(e.kind == kind && (codes.empty() || e.codes == codes), label + ": " + e.what());
            } catch (const std::exception& e) {
                check(false, label + ": unexpected " + e.what());
            }
        };
        rejects(fakeTranslation("kjv"), "bundled id", ImportError::Kind::BundledID);
        rejects(fakeTranslation("bad id!"), "bad id", ImportError::Kind::InvalidFormat);
        rejects("{\"hello\": 1}", "not a translation", ImportError::Kind::InvalidFormat);
        rejects("not json", "not json", ImportError::Kind::InvalidFormat);
        rejects(fakeTranslation("tiny", "Test Version", "TEST", [&](json::Value& o) { versesOf(o).resize(100); }), "too few verses", ImportError::Kind::TooFewVerses);
        rejects(fakeTranslation("books", "Test Version", "TEST", [&](json::Value& o) {
                    for (auto& row : versesOf(o)) {
                        if (row.items()[0].asString() == "JHN") row.items()[0] = json::Value::makeString("XYZ");
                    }
                }),
                "unknown book code", ImportError::Kind::UnknownBooks, {"XYZ"});
        rejects(fakeTranslation("noname", ""), "empty name", ImportError::Kind::InvalidFormat);
        rejects(fakeTranslation("   "), "blank id", ImportError::Kind::InvalidFormat);
        rejects(fakeTranslation("rows", "Test Version", "TEST", [&](json::Value& o) { versesOf(o)[5] = json::Value::makeArray({json::Value::makeString("GEN"), json::Value::makeInt(1)}); }),
                "short verse row", ImportError::Kind::InvalidFormat);
        rejects(fakeTranslation("types", "Test Version", "TEST", [&](json::Value& o) { versesOf(o)[5].items()[1] = json::Value::makeString("1"); }),
                "string chapter", ImportError::Kind::InvalidFormat);
        {
            try {
                importer.validate(fakeTranslation("tiny2", "Test Version", "TEST", [&](json::Value& o) { versesOf(o).resize(10); }));
            } catch (const ImportError& e) {
                check(std::string(e.what()) == "The file holds only 10 verses; a translation needs the whole Bible.", std::string("tooFewVerses message: ") + e.what());
            }
            try {
                importer.validate("{\"hello\": 1}");
            } catch (const ImportError& e) {
                check(std::string(e.what()) == "This is not a BibleTime translation file (The data couldn\xE2\x80\x99t be read because it is missing.). Convert your copy with Tools/build_verses.py first.",
                      std::string("invalidFormat message matches Foundation: ") + e.what());
            }
            try {
                importer.validate(fakeTranslation("kjv"));
            } catch (const ImportError& e) {
                check(std::string(e.what()) == "The id \"kjv\" belongs to a bundled translation; give the file a different id.", std::string("bundledID message: ") + e.what());
            }
            const ImportError many = ImportError::unknownBooks({"AAA", "BBB", "CCC", "DDD", "EEE", "FFF"});
            check(std::string(many.what()) == "Unknown book codes: AAA, BBB, CCC, DDD, EEE. Books must use USFM codes such as JHN." && many.codes.size() == 6, "unknownBooks lists five codes");
        }
        {
            const auto ok = importer.validate(fakeTranslation("Unicode-\xC3\xA9_1"));
            check(ok.first.id == "unicode-\xC3\xA9_1" && ok.second == kjv.verseCount(), "non-ASCII letters are allowed in ids");
        }
        check(fs::exists(info.path), "rejected imports leave existing files alone");

        // Removal, and the bundled ones are protected.
        fs::remove(dropDir / "shadow.json");
        importer.remove(info);
        importer.remove(*importing.catalog().info("kjv"));
        importing.catalog().rescan();
        check(ids(importing.catalog().available()) == std::vector<std::string>({"kjv", "dby", "dropped"}),
              "removed import disappears, bundled stay: " + join(ids(importing.catalog().available())));
        check(!fs::exists(info.path), "file deleted");
        p.translation = "test-1";
        const DisplayContent fallback = importing.content(date(2026, 11, 3, 15, 16), p, us);
        check(fallback.translation == "KJV", "a removed translation falls back to KJV");
        bool removeThrew = false;
        try {
            importer.remove(info);
        } catch (const std::exception&) {
            removeThrew = true;
        }
        check(removeThrew, "removing a missing file throws, like FileManager");
    } catch (const std::exception& e) {
        check(false, std::string("import section threw ") + e.what());
    }
    fs::remove_all(importRoot, ec);

    section("load errors");
    {
        const auto expectLoadError = [&](ResourceLoader loader, const std::string& message) {
            try {
                VerseClock broken(std::move(loader), {});
                check(false, "expected LoadError: " + message);
            } catch (const VerseClock::LoadError& e) {
                check(e.what() == message, std::string("load error: ") + e.what());
            }
        };
        expectLoadError([](const std::string&) { return std::optional<std::string>(); }, "BibleTime resource missing: books.json");
        const ResourceLoader real = directoryLoader(resources);
        expectLoadError([real](const std::string& name) { return name.rfind("Translations/", 0) == 0 ? std::nullopt : real(name); },
                        "BibleTime resource missing: Translations/*.json");
        VerseClock noCurated([real](const std::string& name) { return name == "curated.json" ? std::nullopt : real(name); }, {});
        check(noCurated.curated().bySlot.empty() && noCurated.curated().hourVerses.empty(), "curated lists are optional");
        VerseClock badCurated([real](const std::string& name) { return name == "hour_verses.json" ? std::optional<std::string>("[[\"PSA\", 90]]") : real(name); }, {});
        check(badCurated.curated().bySlot.empty(), "a malformed curated file yields empty lists");
        Preferences p;
        p.hourStyle = HourStyle::TwelveHour;
        check(!badCurated.content(at(15, 16, 99), p, us).text.empty(), "selection still works without curated lists");
        VerseClock onlyDarby([real](const std::string& name) { return name == "Translations/kjv.json" ? std::nullopt : real(name); }, {});
        check(ids(onlyDarby.catalog().available()) == std::vector<std::string>({"dby"}), "only the bundled files the loader has");
        check(onlyDarby.translation(p)->id() == "dby", "kjv preference falls back to the first available");
        const CuratedVerses keys = CuratedVerses::fromJSON(R"({"3::16":[["JHN",3,16]],"13:01":[["GEN",1,1]],"x:1":[],"12:00":[["PSA",1,1,"extra"]]})", "[]");
        check(keys.bySlot.size() == 2 && keys.bySlot.count(3 * 60 + 16) && keys.bySlot.count(12 * 60), "curated keys parse like Swift's split and Int()");
    }

    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
