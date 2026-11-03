import Foundation

/// What the screen shows for one minute.
public struct DisplayContent: Equatable, Sendable {
    public let minuteKey: Int
    public let text: String
    public let reference: String
    public let clock: String
    public let kind: SelectionKind
    public let translation: String

    public init(minuteKey: Int, text: String, reference: String, clock: String, kind: SelectionKind, translation: String) {
        self.minuteKey = minuteKey
        self.text = text
        self.reference = reference
        self.clock = clock
        self.kind = kind
        self.translation = translation
    }
}

/// Loads every resource the saver needs from a directory and turns a `Date` into `DisplayContent`.
public final class VerseClock: @unchecked Sendable {
    public let canon: Canon
    public let catalog: TranslationCatalog
    public let curated: CuratedVerses

    public enum LoadError: Error, CustomStringConvertible {
        case missing(String)
        public var description: String {
            switch self { case .missing(let name): return "BibleTime resource missing: \(name)" }
        }
    }

    /// Directories searched for user-imported translations, most preferred first. The first one is where imports
    /// are written. Inside the screen saver sandbox the Application Support directory resolves to the
    /// legacyScreenSaver container; the second entry is the real home directory, which the sandbox can read, so a
    /// file can also be dropped at `~/Library/Application Support/BibleTime/Translations/` by hand.
    public static func defaultImportDirectories() -> [URL] {
        var urls: [URL] = []
        if let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            urls.append(support.appendingPathComponent("BibleTime/Translations", isDirectory: true))
        }
        if let home = getpwuid(getuid())?.pointee.pw_dir.map({ String(cString: $0) }) {
            urls.append(URL(fileURLWithPath: home).appendingPathComponent("Library/Application Support/BibleTime/Translations", isDirectory: true))
        }
        var unique: [URL] = []
        for url in urls where !unique.contains(where: { $0.standardizedFileURL.path == url.standardizedFileURL.path }) { unique.append(url) }
        return unique
    }

    /// `resources` is the bundle's resource directory: it must contain `books.json`, `curated.json`,
    /// `hour_verses.json` and a `Translations/` folder. `importDirectories` default to `defaultImportDirectories()`.
    public init(resources: URL, importDirectories: [URL]? = nil) throws {
        let books = resources.appendingPathComponent("books.json")
        guard let booksData = FileManager.default.contents(atPath: books.path) else { throw LoadError.missing("books.json") }
        canon = try Canon(data: booksData)
        catalog = TranslationCatalog(canon: canon, translationsDirectory: resources.appendingPathComponent("Translations"),
                                     importDirectories: importDirectories ?? VerseClock.defaultImportDirectories())
        guard !catalog.available.isEmpty else { throw LoadError.missing("Translations/*.json") }
        if let c = FileManager.default.contents(atPath: resources.appendingPathComponent("curated.json").path),
           let h = FileManager.default.contents(atPath: resources.appendingPathComponent("hour_verses.json").path) {
            curated = (try? CuratedVerses(curated: c, hours: h)) ?? .empty
        } else {
            curated = .empty
        }
    }

    /// Imports into the first import directory.
    public var importer: TranslationImporter? {
        guard let destination = catalog.importDirectories.first else { return nil }
        return TranslationImporter(canon: canon, destination: destination)
    }

    /// Resolve the preferred translation, falling back to the first bundled one.
    public func translation(for preferences: Preferences) throws -> Translation {
        if let t = try catalog.translation(id: preferences.translation) { return t }
        throw LoadError.missing("Translations/\(preferences.translation).json")
    }

    /// Content for `date`. Throws only when the translation file cannot be read.
    public func content(for date: Date, preferences: Preferences, calendar: Calendar = .current, locale: Locale = .current, variant: Int = 0) throws -> DisplayContent {
        let translation = try translation(for: preferences)
        let time = ClockTime(date: date, calendar: calendar)
        let use24 = preferences.uses24Hour(locale: locale)
        let selector = VerseSelector(translation: translation, curated: curated)
        let formatter = ReferenceFormatter(canon: canon, pad: preferences.padReference)
        let clock = VerseClock.clockString(for: date, use24Hour: use24, calendar: calendar, locale: locale)

        guard let selection = selector.select(time: time, preferences: preferences, use24Hour: use24, variant: variant) else {
            return DisplayContent(minuteKey: time.minuteKey, text: "", reference: "", clock: clock, kind: .fallback, translation: translation.abbreviation)
        }
        return DisplayContent(
            minuteKey: time.minuteKey,
            text: selection.text,
            reference: formatter.string(for: selection),
            clock: clock,
            kind: selection.kind,
            translation: translation.abbreviation)
    }

    /// Content for a specific verse at `date` (used for screenshots and the options preview). Returns nil when the
    /// reference does not exist in the chosen translation.
    public func content(for reference: VerseRef, date: Date, preferences: Preferences, calendar: Calendar = .current, locale: Locale = .current) throws -> DisplayContent? {
        let translation = try translation(for: preferences)
        guard let verse = translation[reference] else { return nil }
        let selector = VerseSelector(translation: translation, curated: curated)
        let selection = Selection(verses: selector.extended(verse, enabled: preferences.extendFragments), kind: .match)
        let formatter = ReferenceFormatter(canon: canon, pad: preferences.padReference)
        let use24 = preferences.uses24Hour(locale: locale)
        return DisplayContent(
            minuteKey: ClockTime(date: date, calendar: calendar).minuteKey,
            text: selection.text,
            reference: formatter.string(for: selection),
            clock: VerseClock.clockString(for: date, use24Hour: use24, calendar: calendar, locale: locale),
            kind: .match,
            translation: translation.abbreviation)
    }

    /// "3:16 PM" or "15:16" depending on the hour style.
    public static func clockString(for date: Date, use24Hour: Bool, calendar: Calendar = .current, locale: Locale = .current) -> String {
        let f = DateFormatter()
        f.calendar = calendar
        f.timeZone = calendar.timeZone
        f.locale = locale
        f.dateFormat = use24Hour ? "HH:mm" : "h:mm a"
        return f.string(from: date)
    }
}
