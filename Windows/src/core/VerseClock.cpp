// The facade the screen saver talks to. Mirrors Sources/BibleTimeCore/VerseClock.swift.
#include "Core.h"

namespace bt {

std::string clockString(int hour, int minute, bool use24Hour, const LocaleInfo& locale) {
    const auto two = [](int n) { return (n < 10 ? "0" : "") + std::to_string(n); };
    if (use24Hour) return two(hour) + ":" + two(minute);  // "HH:mm"
    const int h12 = hour % 12 == 0 ? 12 : hour % 12;      // "h:mm a"
    return std::to_string(h12) + ":" + two(minute) + " " + (hour < 12 ? locale.amSymbol : locale.pmSymbol);
}

VerseClock::VerseClock(ResourceLoader loader, std::vector<std::filesystem::path> importDirectories) {
    const auto booksText = loader ? loader("books.json") : std::nullopt;
    if (!booksText) throw LoadError("BibleTime resource missing: books.json");
    canon_ = Canon::fromJSON(*booksText);
    catalog_ = std::make_unique<TranslationCatalog>(canon_, loader, std::move(importDirectories));
    if (catalog_->available().empty()) throw LoadError("BibleTime resource missing: Translations/*.json");
    const auto curatedText = loader("curated.json");
    const auto hoursText = loader("hour_verses.json");
    if (curatedText && hoursText) {
        try {
            curated_ = CuratedVerses::fromJSON(*curatedText, *hoursText);
        } catch (const std::exception&) {
            curated_ = CuratedVerses();
        }
    }
}

std::optional<TranslationImporter> VerseClock::importer() const {
    if (catalog_->importDirectories().empty()) return std::nullopt;
    return TranslationImporter(canon_, catalog_->importDirectories().front());
}

std::shared_ptr<const Translation> VerseClock::translation(const Preferences& preferences) {
    if (auto t = catalog_->translation(preferences.translation)) return t;
    throw LoadError("BibleTime resource missing: Translations/" + preferences.translation + ".json");
}

DisplayContent VerseClock::content(const ClockTime& time, const Preferences& preferences, const LocaleInfo& locale,
                                   int variant) {
    const auto t = translation(preferences);
    const bool use24 = preferences.uses24Hour(locale.uses24Hour);
    const VerseSelector selector(*t, curated_);
    const ReferenceFormatter formatter(canon_, preferences.padReference);
    DisplayContent result;
    result.minuteKey = time.minuteKey();
    result.clock = clockString(time.hour, time.minute, use24, locale);
    result.translation = t->abbreviation();
    const auto selection = selector.select(time, preferences, use24, variant);
    if (!selection) {
        result.kind = SelectionKind::Fallback;
        return result;
    }
    result.text = selection->text();
    result.reference = formatter.string(*selection);
    result.kind = selection->kind;
    return result;
}

std::optional<DisplayContent> VerseClock::content(const VerseRef& reference, const ClockTime& time,
                                                  const Preferences& preferences, const LocaleInfo& locale) {
    const auto t = translation(preferences);
    const Verse* verse = t->find(reference);
    if (!verse) return std::nullopt;
    const VerseSelector selector(*t, curated_);
    Selection selection;
    selection.verses = selector.extended(*verse, preferences.extendFragments);
    selection.kind = SelectionKind::Match;
    const ReferenceFormatter formatter(canon_, preferences.padReference);
    const bool use24 = preferences.uses24Hour(locale.uses24Hour);
    DisplayContent result;
    result.minuteKey = time.minuteKey();
    result.text = selection.text();
    result.reference = formatter.string(selection);
    result.clock = clockString(time.hour, time.minute, use24, locale);
    result.kind = SelectionKind::Match;
    result.translation = t->abbreviation();
    return result;
}

}  // namespace bt
