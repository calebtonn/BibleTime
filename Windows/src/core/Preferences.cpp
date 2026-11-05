// Preferences and their enums. Mirrors Sources/BibleTimeCore/Preferences.swift.
#include <algorithm>

#include "Core.h"

namespace bt {

const char* rawValue(HourStyle v) {
    switch (v) {
        case HourStyle::System: return "system";
        case HourStyle::TwelveHour: return "twelveHour";
        case HourStyle::TwentyFourHour: return "twentyFourHour";
    }
    return "system";
}

const char* rawValue(ThemeChoice v) {
    switch (v) {
        case ThemeChoice::Auto: return "auto";
        case ThemeChoice::Paper: return "paper";
        case ThemeChoice::Night: return "night";
    }
    return "auto";
}

const char* rawValue(LayoutStyle v) {
    switch (v) {
        case LayoutStyle::Clock: return "clock";
        case LayoutStyle::Centered: return "centered";
        case LayoutStyle::Book: return "book";
    }
    return "clock";
}

const char* rawValue(TypefaceChoice v) {
    switch (v) {
        case TypefaceChoice::NewYork: return "newYork";
        case TypefaceChoice::Charter: return "charter";
        case TypefaceChoice::Palatino: return "palatino";
        case TypefaceChoice::Georgia: return "georgia";
        case TypefaceChoice::Sans: return "sans";
    }
    return "newYork";
}

const char* rawValue(DisplayMode v) {
    switch (v) {
        case DisplayMode::MainOnly: return "mainOnly";
        case DisplayMode::SameVerse: return "sameVerse";
        case DisplayMode::DifferentVerses: return "differentVerses";
    }
    return "sameVerse";
}

template <> const std::vector<HourStyle>& allCases<HourStyle>() {
    static const std::vector<HourStyle> v = {HourStyle::System, HourStyle::TwelveHour, HourStyle::TwentyFourHour};
    return v;
}
template <> const std::vector<ThemeChoice>& allCases<ThemeChoice>() {
    static const std::vector<ThemeChoice> v = {ThemeChoice::Auto, ThemeChoice::Paper, ThemeChoice::Night};
    return v;
}
template <> const std::vector<LayoutStyle>& allCases<LayoutStyle>() {
    static const std::vector<LayoutStyle> v = {LayoutStyle::Clock, LayoutStyle::Centered, LayoutStyle::Book};
    return v;
}
template <> const std::vector<TypefaceChoice>& allCases<TypefaceChoice>() {
    static const std::vector<TypefaceChoice> v = {TypefaceChoice::NewYork, TypefaceChoice::Charter, TypefaceChoice::Palatino,
                                                  TypefaceChoice::Georgia, TypefaceChoice::Sans};
    return v;
}
template <> const std::vector<DisplayMode>& allCases<DisplayMode>() {
    static const std::vector<DisplayMode> v = {DisplayMode::MainOnly, DisplayMode::SameVerse, DisplayMode::DifferentVerses};
    return v;
}

namespace {
/// Case-sensitive raw value lookup, like Swift's `init?(rawValue:)`.
template <class E> std::optional<E> fromRaw(std::string_view s) {
    for (E e : allCases<E>()) {
        if (s == rawValue(e)) return e;
    }
    return std::nullopt;
}
}  // namespace

std::optional<HourStyle> hourStyleFrom(std::string_view s) { return fromRaw<HourStyle>(s); }
std::optional<ThemeChoice> themeFrom(std::string_view s) { return fromRaw<ThemeChoice>(s); }
std::optional<LayoutStyle> layoutFrom(std::string_view s) { return fromRaw<LayoutStyle>(s); }
std::optional<TypefaceChoice> typefaceFrom(std::string_view s) { return fromRaw<TypefaceChoice>(s); }
std::optional<DisplayMode> displayModeFrom(std::string_view s) { return fromRaw<DisplayMode>(s); }

const char* title(HourStyle v) {
    switch (v) {
        case HourStyle::System: return "Follow System";
        case HourStyle::TwelveHour: return "12-hour";
        case HourStyle::TwentyFourHour: return "24-hour";
    }
    return "";
}

const char* title(ThemeChoice v) {
    switch (v) {
        case ThemeChoice::Auto: return "Automatic";
        case ThemeChoice::Paper: return "Paper";
        case ThemeChoice::Night: return "Night";
    }
    return "";
}

const char* title(LayoutStyle v) {
    switch (v) {
        case LayoutStyle::Clock: return "Clock";
        case LayoutStyle::Centered: return "Centered";
        case LayoutStyle::Book: return "Book";
    }
    return "";
}

const char* title(DisplayMode v) {
    switch (v) {
        case DisplayMode::MainOnly: return "Main display only";
        case DisplayMode::SameVerse: return "All displays, same verse";
        case DisplayMode::DifferentVerses: return "All displays, different verses";
    }
    return "";
}

Preferences Preferences::normalized() const {
    Preferences p = *this;
    p.textScale = std::min(std::max(p.textScale, textScaleMin), textScaleMax);
    return p;
}

Preferences Preferences::fromJSON(const json::Value& d) {
    Preferences p;
    const auto string = [&d](const char* key) -> const std::string* {
        const json::Value* v = d.find(key);
        return v && v->isString() ? &v->asString() : nullptr;
    };
    // JSONSerialization hands Swift NSNumbers: `as? Bool` accepts true/false and any number equal to 0 or 1
    // (0, 1, 0.0, 1.0, 1e0), and `as? NSNumber` accepts booleans too (true is 1.0, false 0.0).
    const auto boolean = [&d](const char* key, bool& out) {
        const json::Value* v = d.find(key);
        if (!v) return;
        if (v->isBool()) out = v->asBool();
        else if (v->isNumber() && (v->asDouble() == 0 || v->asDouble() == 1)) out = v->asDouble() == 1;
    };
    if (const auto* v = string("translation"); v && !v->empty()) p.translation = *v;
    if (const auto* v = string("hourStyle")) { if (auto e = hourStyleFrom(*v)) p.hourStyle = *e; }
    if (const auto* v = string("theme")) { if (auto e = themeFrom(*v)) p.theme = *e; }
    if (const auto* v = string("layout")) { if (auto e = layoutFrom(*v)) p.layout = *e; }
    if (const auto* v = string("typeface")) { if (auto e = typefaceFrom(*v)) p.typeface = *e; }
    if (const json::Value* v = d.find("textScale")) {
        if (v->isNumber()) p.textScale = v->asDouble();
        else if (v->isBool()) p.textScale = v->asBool() ? 1.0 : 0.0;
    }
    boolean("showClock", p.showClock);
    boolean("preferFamous", p.preferFamous);
    boolean("extendFragments", p.extendFragments);
    boolean("padReference", p.padReference);
    if (const auto* v = string("displayMode")) { if (auto e = displayModeFrom(*v)) p.displayMode = *e; }
    return p.normalized();
}

Preferences Preferences::fromJSONText(std::string_view text) {
    try {
        return fromJSON(json::parse(text));
    } catch (const json::ParseError&) {
        return Preferences();
    }
}

json::Value Preferences::toJSON() const {
    // Key order follows Swift's Preferences.Key.
    return json::Value::makeObject({
        {"translation", json::Value::makeString(translation)},
        {"hourStyle", json::Value::makeString(rawValue(hourStyle))},
        {"theme", json::Value::makeString(rawValue(theme))},
        {"layout", json::Value::makeString(rawValue(layout))},
        {"typeface", json::Value::makeString(rawValue(typeface))},
        {"textScale", json::Value::makeNumber(textScale)},
        {"showClock", json::Value::makeBool(showClock)},
        {"preferFamous", json::Value::makeBool(preferFamous)},
        {"extendFragments", json::Value::makeBool(extendFragments)},
        {"padReference", json::Value::makeBool(padReference)},
        {"displayMode", json::Value::makeString(rawValue(displayMode))},
    });
}

std::string Preferences::toJSONText() const { return json::serialize(toJSON(), 2) + "\n"; }

bool Preferences::uses24Hour(bool localeUses24Hour) const {
    switch (hourStyle) {
        case HourStyle::TwelveHour: return false;
        case HourStyle::TwentyFourHour: return true;
        case HourStyle::System: return localeUses24Hour;
    }
    return localeUses24Hour;
}

bool Preferences::operator==(const Preferences& o) const {
    return translation == o.translation && hourStyle == o.hourStyle && theme == o.theme && layout == o.layout &&
           typeface == o.typeface && textScale == o.textScale && showClock == o.showClock &&
           preferFamous == o.preferFamous && extendFragments == o.extendFragments && padReference == o.padReference &&
           displayMode == o.displayMode;
}

}  // namespace bt
