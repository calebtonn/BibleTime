import Foundation

/// How the hour is mapped to a chapter.
public enum HourStyle: String, Codable, CaseIterable, Sendable {
    /// Follow the system clock format (12-hour or 24-hour).
    case system
    /// 1–12 always maps to chapters 1–12 (13:16 shows a chapter 1 verse).
    case twelveHour
    /// 13:16 prefers chapter 13; falls back to chapter 1 when no verse exists.
    case twentyFourHour

    public var title: String {
        switch self {
        case .system: return "Follow System"
        case .twelveHour: return "12-hour"
        case .twentyFourHour: return "24-hour"
        }
    }
}

public enum Theme: String, Codable, CaseIterable, Sendable {
    case auto, paper, night

    public var title: String {
        switch self {
        case .auto: return "Automatic"
        case .paper: return "Paper"
        case .night: return "Night"
        }
    }
}

public enum LayoutStyle: String, Codable, CaseIterable, Sendable {
    /// Verse top-left, reference bottom-right, like the physical Bible Clock.
    case clock
    /// Verse centered with the reference beneath it.
    case centered
    /// A left column of text with a hairline rule.
    case book

    public var title: String {
        switch self {
        case .clock: return "Clock"
        case .centered: return "Centered"
        case .book: return "Book"
        }
    }
}

public enum TypefaceChoice: String, Codable, CaseIterable, Sendable {
    case newYork, charter, palatino, georgia, sans

    public var title: String {
        switch self {
        case .newYork: return "New York"
        case .charter: return "Charter"
        case .palatino: return "Palatino"
        case .georgia: return "Georgia"
        case .sans: return "San Francisco"
        }
    }
}

/// What to show when more than one display is attached.
public enum DisplayMode: String, Codable, CaseIterable, Sendable {
    /// Verses on the main display; other displays show only the background.
    case mainOnly
    /// Every display shows the same verse (the default).
    case sameVerse
    /// Every display shows a different verse for the minute, when the Bible has more than one.
    case differentVerses

    public var title: String {
        switch self {
        case .mainOnly: return "Main display only"
        case .sameVerse: return "All displays, same verse"
        case .differentVerses: return "All displays, different verses"
        }
    }
}

/// All user-adjustable settings. Stored by the screen saver in `ScreenSaverDefaults`; tests and the preview host
/// can construct them directly.
public struct Preferences: Codable, Equatable, Sendable {
    public var translation: String = "kjv"
    public var hourStyle: HourStyle = .system
    public var theme: Theme = .auto
    public var layout: LayoutStyle = .clock
    public var typeface: TypefaceChoice = .newYork
    /// Multiplier on the default body size (1.0 = 4% of the screen height). Clamped to `textScaleRange`.
    public var textScale: Double = 1.0
    public var showClock: Bool = true
    /// Show a well-known verse (John 3:16 at 3:16) whenever one exists for the minute.
    public var preferFamous: Bool = true
    /// When a verse ends mid-sentence, append the next verse so the thought completes.
    public var extendFragments: Bool = true
    /// Zero-pad chapter and verse in the reference ("John 03:16") so it reads like the time.
    public var padReference: Bool = true
    public var displayMode: DisplayMode = .sameVerse

    public static let textScaleRange: ClosedRange<Double> = 0.75...2.0

    public init() {}

    /// A copy with every value clamped to its legal range.
    public var normalized: Preferences {
        var p = self
        p.textScale = min(max(p.textScale, Preferences.textScaleRange.lowerBound), Preferences.textScaleRange.upperBound)
        return p
    }

    // MARK: Key-value representation (used for ScreenSaverDefaults and the dev override environment variable)

    public enum Key: String, CaseIterable {
        case translation, hourStyle, theme, layout, typeface, textScale, showClock, preferFamous, extendFragments, padReference, displayMode
    }

    public var dictionary: [String: Any] {
        [
            Key.translation.rawValue: translation,
            Key.hourStyle.rawValue: hourStyle.rawValue,
            Key.theme.rawValue: theme.rawValue,
            Key.layout.rawValue: layout.rawValue,
            Key.typeface.rawValue: typeface.rawValue,
            Key.textScale.rawValue: textScale,
            Key.showClock.rawValue: showClock,
            Key.preferFamous.rawValue: preferFamous,
            Key.extendFragments.rawValue: extendFragments,
            Key.padReference.rawValue: padReference,
            Key.displayMode.rawValue: displayMode.rawValue,
        ]
    }

    /// Build preferences from a loosely typed dictionary (missing or malformed keys keep their defaults).
    public init(dictionary d: [String: Any]) {
        self.init()
        if let v = d[Key.translation.rawValue] as? String, !v.isEmpty { translation = v }
        if let v = d[Key.hourStyle.rawValue] as? String, let e = HourStyle(rawValue: v) { hourStyle = e }
        if let v = d[Key.theme.rawValue] as? String, let e = Theme(rawValue: v) { theme = e }
        if let v = d[Key.layout.rawValue] as? String, let e = LayoutStyle(rawValue: v) { layout = e }
        if let v = d[Key.typeface.rawValue] as? String, let e = TypefaceChoice(rawValue: v) { typeface = e }
        if let v = d[Key.textScale.rawValue] as? NSNumber { textScale = v.doubleValue }
        if let v = d[Key.showClock.rawValue] as? Bool { showClock = v }
        if let v = d[Key.preferFamous.rawValue] as? Bool { preferFamous = v }
        if let v = d[Key.extendFragments.rawValue] as? Bool { extendFragments = v }
        if let v = d[Key.padReference.rawValue] as? Bool { padReference = v }
        if let v = d[Key.displayMode.rawValue] as? String, let e = DisplayMode(rawValue: v) { displayMode = e }
        self = normalized
    }

    /// Whether the clock should be read as 24-hour for the given style and locale.
    public func uses24Hour(locale: Locale = .current) -> Bool {
        switch hourStyle {
        case .twelveHour: return false
        case .twentyFourHour: return true
        case .system: return Preferences.localeUses24Hour(locale)
        }
    }

    public static func localeUses24Hour(_ locale: Locale) -> Bool {
        guard let format = DateFormatter.dateFormat(fromTemplate: "j", options: 0, locale: locale) else { return false }
        return format.contains("H") || format.contains("k")
    }
}
