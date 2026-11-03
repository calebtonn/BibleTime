import Foundation

/// Environment hooks used by `Tools/SaverHost.swift` and the Makefile `render` target to produce deterministic
/// screenshots. They have no effect unless the variables are set, which never happens inside System Settings.
///
/// - `BIBLETIME_TIME="15:16"` pins the clock to today at that time.
/// - `BIBLETIME_PREFS='{"layout":"book"}'` overrides stored settings (see `PreferencesStore`).
enum DevOverrides {
    static let pinnedDate: Date? = {
        guard let value = ProcessInfo.processInfo.environment["BIBLETIME_TIME"] else { return nil }
        let parts = value.split(separator: ":").compactMap { Int($0) }
        guard parts.count == 2, (0...23).contains(parts[0]), (0...59).contains(parts[1]) else { return nil }
        var components = Calendar.current.dateComponents([.year, .month, .day], from: Date())
        components.hour = parts[0]
        components.minute = parts[1]
        components.second = 0
        return Calendar.current.date(from: components)
    }()

    static func now() -> Date { pinnedDate ?? Date() }

    /// - `BIBLETIME_DISPLAY=1` pretends the view sits on the second display (0 is the main display).
    static let pinnedDisplayIndex: Int? = {
        guard let value = ProcessInfo.processInfo.environment["BIBLETIME_DISPLAY"], let index = Int(value), index >= 0 else { return nil }
        return index
    }()

    /// - `BIBLETIME_REF="EST 8:9"` (USFM book code) forces a verse regardless of the time, for screenshots.
    static let pinnedReference: VerseRef? = {
        guard let value = ProcessInfo.processInfo.environment["BIBLETIME_REF"] else { return nil }
        let parts = value.split(separator: " ")
        guard parts.count == 2 else { return nil }
        let numbers = parts[1].split(separator: ":").compactMap { Int($0) }
        guard numbers.count == 2 else { return nil }
        return VerseRef(book: String(parts[0]).uppercased(), chapter: numbers[0], verse: numbers[1])
    }()
}
