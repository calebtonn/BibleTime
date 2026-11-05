// Cross-check between the C++ port and the Swift core (Tests/CrossCheck/main.swift).
// Both programs take the Resources directory and must print byte-identical text; `make crosscheck` diffs them.
// Keep the two files in lockstep: same sections, same loops in the same order, same formatting.
//
//   crosscheck <ResourcesDir> > out.txt
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Core.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace bt;
namespace fs = std::filesystem;

namespace {

std::string out;

void flush() {
    std::fwrite(out.data(), 1, out.size(), stdout);
    out.clear();
}

void emit(const std::string& line) {
    out += line;
    out += '\n';
    if (out.size() >= (1u << 20)) flush();
}

std::string pad(int64_t n, size_t width) {
    std::string s = std::to_string(n);
    return s.size() >= width ? s : std::string(width - s.size(), '0') + s;
}

std::string hex16(uint64_t v) {
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(v));
    return buffer;
}

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : s) h = (h ^ static_cast<uint64_t>(static_cast<unsigned char>(c))) * 0x100000001b3ull;
    return h;
}

const char* kindLetter(SelectionKind kind) {
    switch (kind) {
        case SelectionKind::Match: return "M";
        case SelectionKind::TopOfHour: return "T";
        case SelectionKind::Fallback: return "F";
    }
    return "?";
}

// Howard Hinnant's civil date algorithms (proleptic Gregorian), used only to enumerate the same dates as Swift.
int64_t daysFromCivil(int64_t year, int64_t month, int64_t day) {
    const int64_t y = month <= 2 ? year - 1 : year;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct Civil {
    int year, month, day;
};

Civil civilFromDays(int64_t days) {
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp < 10 ? mp + 3 : mp - 9;
    return {static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0)), static_cast<int>(m), static_cast<int>(d)};
}

std::string dateString(int y, int m, int d) { return pad(y, 4) + "-" + pad(m, 2) + "-" + pad(d, 2); }

ClockTime at(int hour, int minute, int64_t day) {
    ClockTime t;
    t.hour = hour;
    t.minute = minute;
    t.dayOrdinal = day;
    return t;
}

/// Number of Unicode scalars (UTF-8 lead bytes).
size_t scalarCount(const std::string& s) {
    size_t n = 0;
    for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Byte-identical output: no "\n" -> "\r\n" translation.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const fs::path resources = fs::u8path(argc > 1 ? argv[1] : "Resources");
    std::unique_ptr<VerseClock> clockPtr;
    try {
        clockPtr = std::make_unique<VerseClock>(directoryLoader(resources), std::vector<fs::path>{});
    } catch (const std::exception& e) {
        std::fprintf(stderr, "crosscheck: could not load resources at %s: %s\n", resources.u8string().c_str(), e.what());
        return 1;
    }
    VerseClock& clock = *clockPtr;
    const auto loadTranslation = [&clock](const std::string& id) {
        std::shared_ptr<const Translation> t;
        try {
            t = clock.catalog().translation(id);
        } catch (const std::exception&) {
        }
        if (!t || t->id() != id) {
            std::fprintf(stderr, "crosscheck: translation %s did not load\n", id.c_str());
            std::exit(1);
        }
        return t;
    };
    const std::vector<std::string> translationIDs = {"kjv", "web", "asv"};
    const LocaleInfo enUS{false, "AM", "PM"};

    emit("# BibleTime cross-check 1");

    // (a) Selection
    {
        std::vector<std::pair<std::string, Preferences>> combos;
        combos.emplace_back("default", Preferences());
        Preferences p;
        p.preferFamous = false;
        combos.emplace_back("noFamous", p);
        p = Preferences();
        p.extendFragments = false;
        combos.emplace_back("noExtend", p);
        p = Preferences();
        p.padReference = false;
        combos.emplace_back("noPad", p);
        const int64_t days[] = {0, 1, 2, 3, 100, 739925, 740288, -5};

        for (const std::string& id : translationIDs) {
            const auto translation = loadTranslation(id);
            const VerseSelector selector(*translation, clock.curated());
            for (size_t index = 0; index < combos.size(); ++index) {
                const auto& combo = combos[index];
                const ReferenceFormatter formatter(clock.canon(), combo.second.padReference);
                const std::vector<int> variants = index == 0 ? std::vector<int>{0, 1, 2} : std::vector<int>{0};
                for (const bool use24 : {false, true}) {
                    emit("## selection " + id + " " + combo.first + " " + (use24 ? "24h" : "12h"));
                    for (const int64_t day : days) {
                        emit("# day " + std::to_string(day));
                        for (int minuteKey = 0; minuteKey < 1440; ++minuteKey) {
                            const ClockTime time = at(minuteKey / 60, minuteKey % 60, day);
                            for (const int variant : variants) {
                                const std::string prefix = pad(time.hour, 2) + ":" + pad(time.minute, 2) + " " + std::to_string(variant) + " ";
                                const auto s = selector.select(time, combo.second, use24, variant);
                                if (!s) {
                                    emit(prefix + "nil");
                                    continue;
                                }
                                std::string refs;
                                for (size_t i = 0; i < s->verses.size(); ++i) {
                                    if (i) refs += ' ';
                                    refs += s->verses[i]->ref.description();
                                }
                                emit(prefix + kindLetter(s->kind) + " " + formatter.string(*s) + "|" + refs + "|" + hex16(fnv1a(s->text())));
                            }
                        }
                    }
                }
            }
        }
    }

    // (b) Day ordinals
    {
        emit("## day ordinals");
        std::vector<Civil> dates;
        const int64_t last = daysFromCivil(2200, 12, 31);
        for (int64_t z = daysFromCivil(1600, 1, 1); z <= last; z += 23) dates.push_back(civilFromDays(z));
        const Civil extra[] = {{1582, 10, 1}, {1582, 10, 4}, {1582, 10, 15}, {1582, 10, 16}, {1970, 1, 1},
                               {2000, 2, 28}, {2000, 2, 29}, {2000, 3, 1},  {2026, 11, 3},  {2026, 11, 5},  {2100, 3, 1}};
        dates.insert(dates.end(), std::begin(extra), std::end(extra));
        for (const Civil& c : dates) {
            emit(dateString(c.year, c.month, c.day) + " " + std::to_string(ClockTime::dayOrdinalFor(c.year, c.month, c.day)));
        }
    }

    // (c) Seeded generator and Swift's next(upperBound:)
    {
        emit("## generator");
        const uint64_t seeds[] = {0, 1, 42, 123456789, 0x9E3779B97F4A7C15ull, UINT64_MAX};
        const uint64_t bounds[] = {1, 2, 3, 5, 6, 7, 10, 100, 1000003, 4294967297ull, 9223372036854775808ull, 9223372036854775809ull, UINT64_MAX};
        for (const uint64_t seed : seeds) {
            SeededGenerator g(seed);
            // Separate statements: the evaluation order of function arguments is unspecified in C++.
            const uint64_t n1 = g.next();
            const uint64_t n2 = g.next();
            const uint64_t n3 = g.next();
            const uint64_t n4 = g.next();
            emit("seed " + std::to_string(seed) + " next " + std::to_string(n1) + " " + std::to_string(n2) + " " + std::to_string(n3) + " " + std::to_string(n4));
            for (const uint64_t bound : bounds) {
                const uint64_t a = g.next(bound);
                const uint64_t b = g.next(bound);
                const uint64_t c = g.next(bound);
                emit("bound " + std::to_string(bound) + " " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c));
            }
            for (const size_t count : {size_t{1}, size_t{2}, size_t{3}, size_t{17}, size_t{26}, size_t{1000}}) {
                const size_t a = g.randomIndex(count);
                const size_t b = g.randomIndex(count);
                const size_t c = g.randomIndex(count);
                emit("element " + std::to_string(count) + " " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c));
            }
            // randomElement on an empty array returns nil without drawing.
            const uint64_t after = g.next();
            emit("empty nil then " + std::to_string(after));
        }
        const std::vector<std::string> salts = {"kjv", "web#1", "asv#2", "", "\xC3\xA9t\xC3\xA9", "test-1"};
        const ClockTime times[] = {at(0, 0, 0), at(15, 16, 739924), at(23, 59, -5), at(12, 30, INT64_MAX), at(1, 1, INT64_MIN)};
        for (size_t ti = 0; ti < sizeof times / sizeof times[0]; ++ti) {
            for (size_t si = 0; si < salts.size(); ++si) {
                SeededGenerator g(times[ti], salts[si]);
                const uint64_t a = g.next();
                const uint64_t b = g.next();
                emit("time " + std::to_string(ti) + " salt " + std::to_string(si) + " " + std::to_string(a) + " " + std::to_string(b));
            }
        }
    }

    // (d) Character counts
    for (const std::string& id : translationIDs) {
        const auto translation = loadTranslation(id);
        size_t verses = 0, characters = 0, scalars = 0;
        std::vector<std::string> differing;
        for (const Book& book : clock.canon().books()) {
            for (int chapter = 1; chapter <= std::max(book.chapters, 1); ++chapter) {
                for (int number = 1; number <= 200; ++number) {
                    const Verse* verse = translation->find(VerseRef(book.code, chapter, number));
                    if (!verse) continue;
                    ++verses;
                    const size_t c = characterCount(verse->text);
                    const size_t s = scalarCount(verse->text);
                    characters += c;
                    scalars += s;
                    if (c != s) differing.push_back(verse->ref.description() + " " + std::to_string(c) + " " + std::to_string(s));
                }
            }
        }
        emit("## characters " + id + " verses " + std::to_string(verses) + " characters " + std::to_string(characters) + " scalars " + std::to_string(scalars));
        for (const auto& line : differing) emit(line);
    }

    // (e) Reference parsing
    {
        emit("## parse");
        const char* inputs[] = {
            "John 3:16", "1 John 4:8", "Psalms 23:1", "Psalm 119:105", "Song of Solomon 2:4", "song of songs 2:4", "Canticles 1:1",
            "Revelation 22:21", "Revelations 1:1", "Apocalypse 1:1", "I Samuel 3:10", "II Kings 2:11", "iii john 1:2", "1st John 1:9",
            "Gen 1:1", "GEN 1:1", "jhn 3:16", "1 Cor. 13:4", "Phil 4:13", "  Romans 8:28  ", "Romans  8:28", "Jude 1:24", "John 3:+16",
            "John 3:-1", "John 3:16a", "John :16", "John 3:", "John", "Nowhere 1:1", "", ":", "3:16", "John 3:16:17",
            "Matthew 99999999999999999999:1", "Ps. 23:1", "Song 2:4", "1 Jn 4:8", "1 Thess 5:17", "Phlm 1:6", "\tEccl 3:1\t",
        };
        for (const char* input : inputs) {
            const auto parsed = clock.canon().parse(input);
            emit(std::string("[") + input + "] " + (parsed ? parsed->description() : "nil"));
        }
    }

    // (f) Clock strings and the facade
    {
        emit("## clock");
        for (int minuteKey = 0; minuteKey < 1440; ++minuteKey) {
            const int h = minuteKey / 60, m = minuteKey % 60;
            emit(clockString(h, m, false, enUS) + "|" + clockString(h, m, true, enUS));
        }

        emit("## facade");
        Preferences prefs;
        prefs.hourStyle = HourStyle::TwelveHour;
        const int64_t start = daysFromCivil(2026, 1, 1);
        const std::pair<int, int> moments[] = {{15, 16}, {0, 0}, {8, 38}, {21, 7}};
        for (int step = 0; step < 53; ++step) {
            const Civil c = civilFromDays(start + step * 7);
            for (const auto& moment : moments) {
                try {
                    const ClockTime time = at(moment.first, moment.second, ClockTime::dayOrdinalFor(c.year, c.month, c.day));
                    const DisplayContent content = clock.content(time, prefs, enUS);
                    emit(dateString(c.year, c.month, c.day) + " " + std::to_string(content.minuteKey) + " " + content.clock + " " +
                         kindLetter(content.kind) + " " + content.reference + " " + content.translation + " " + hex16(fnv1a(content.text)));
                } catch (const std::exception&) {
                    emit(dateString(c.year, c.month, c.day) + " nil");
                }
            }
        }
    }

    // (g) Character counts (String.count) of non-Latin samples: imported translations in other scripts
    {
        emit("## character samples");
        const std::pair<const char*, const char*> samples[] = {
            {"tamil-verse", "தேவன் உலகத்தில் அன்புகூர்ந்தார்,"},
            {"tamil-word", "பைபிள்"},
            {"hindi-word", "हिन्दी"},
            {"hindi-conjunct", "क्षत्रिय"},
            {"hindi-verse", "क्योंकि परमेश्वर ने जगत से ऐसा प्रेम रखा"},
            {"bengali", "ঈশ্বর জগৎকে এত প্রেম করিলেন"},
            {"malayalam", "ദൈവം ലോകത്തെ സ്നേഹിച്ചു"},
            {"telugu", "దేవుడు లోకమును ప్రేమించెను"},
            {"thai-verse", "พระเจ้าทรงรักโลก"},
            {"thai-am", "กำลังน้ำ"},
            {"amharic", "እግዚአብሔር ዓለሙን"},
            {"korean-verse", "하나님이 세상을 이처럼 사랑하사"},
            {"korean-jamo", "\u1100\u1161\u11A8\u1100\u1161"},
            {"korean-jamo-mixed", "\u1100\u1100\u1161가\u11A8각\u1161"},
            {"chinese", "神爱世人，甚至将他的独生子赐给他们"},
            {"arabic-verse", "لأَنَّهُ هكَذَا أَحَبَّ اللهُ الْعَالَمَ"},
            {"arabic-prepend", "\u0600١٢ \u06DD٣"},
            {"hebrew-points", "כִּי־כֵן אָהַב אֱלֹהִים אֶת־הָעוֹלָם"},
            {"greek-polytonic", "Οὕτως γὰρ ἠγάπησεν ὁ θεὸς τὸν κόσμον"},
            {"combining-stack", "e\u0301\u0302o\u0308\u0323 a\u20DD"},
            {"crlf", "a\r\nb\r\n\n\rc\r\u0301"},
            {"emoji-family", "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466"},
            {"emoji-skin", "\U0001F44D\U0001F3FD\U0001F44D \U0001F9D1\U0001F3FE\u200D\U0001F91D\u200D\U0001F9D1\U0001F3FB"},
            {"emoji-rainbow-vs16", "\U0001F3F3\uFE0F\u200D\U0001F308 \u2764\uFE0F 1\uFE0F\u20E3"},
            {"flags", "\U0001F1FA\U0001F1F8\U0001F1EC\U0001F1E7\U0001F1E9"},
            {"flag-tags", "\U0001F3F4\U000E0067\U000E0062\U000E0073\U000E0063\U000E0074\U000E007F"},
            {"zwj-after-letter", "a\u200D\U0001F44D \u200D\u200D"},
        };
        for (const auto& sample : samples) {
            emit(std::string(sample.first) + " " + std::to_string(characterCount(sample.second)) + " " +
                 std::to_string(scalarCount(sample.second)));
        }
    }

    emit("# end");
    flush();
    return 0;
}
