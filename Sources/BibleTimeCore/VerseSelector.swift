import Foundation

/// Curated lists shipped in `Resources/curated.json` and `Resources/hour_verses.json`.
public struct CuratedVerses: Sendable {
    /// Keyed by `hour12 * 60 + minute` (hour 1–12), most famous first.
    public let bySlot: [Int: [VerseRef]]
    /// Verses about time, mornings and seasons, shown at the top of the hour.
    public let hourVerses: [VerseRef]

    public init(bySlot: [Int: [VerseRef]], hourVerses: [VerseRef]) {
        self.bySlot = bySlot
        self.hourVerses = hourVerses
    }

    public static let empty = CuratedVerses(bySlot: [:], hourVerses: [])

    /// Decode the two JSON files. `curated` is `{"H:MM": [["JHN",3,16], ...]}`, `hours` is `[["PSA",90,12], ...]`.
    public init(curated: Data, hours: Data) throws {
        let slots = try JSONDecoder().decode([String: [RefRow]].self, from: curated)
        var bySlot: [Int: [VerseRef]] = [:]
        for (key, rows) in slots {
            let parts = key.split(separator: ":")
            guard parts.count == 2, let h = Int(parts[0]), let m = Int(parts[1]), (1...12).contains(h), (0...59).contains(m) else { continue }
            bySlot[h * 60 + m] = rows.map(\.ref)
        }
        self.bySlot = bySlot
        self.hourVerses = try JSONDecoder().decode([RefRow].self, from: hours).map(\.ref)
    }

    private struct RefRow: Decodable {
        let ref: VerseRef
        init(from decoder: Decoder) throws {
            var c = try decoder.unkeyedContainer()
            let book = try c.decode(String.self)
            let chapter = try c.decode(Int.self)
            let verse = try c.decode(Int.self)
            ref = VerseRef(book: book, chapter: chapter, verse: verse)
        }
    }
}

/// Why a particular verse was chosen for a minute.
public enum SelectionKind: Equatable, Sendable {
    /// Chapter and verse match the time.
    case match
    /// Top of the hour: a verse about time.
    case topOfHour
    /// No verse exists for this chapter:verse; a verse about time stands in.
    case fallback
}

/// The verse(s) to display for one minute.
public struct Selection: Equatable, Sendable {
    public let verses: [Verse]
    public let kind: SelectionKind

    public init(verses: [Verse], kind: SelectionKind) {
        self.verses = verses
        self.kind = kind
    }

    public var primary: Verse { verses[0] }
    public var text: String { verses.map(\.text).joined(separator: " ") }
    public var isExtended: Bool { verses.count > 1 }
}

/// A clock moment reduced to what selection needs.
public struct ClockTime: Equatable, Sendable {
    public let hour: Int      // 0–23
    public let minute: Int    // 0–59
    /// Days since an arbitrary epoch in the local calendar. Drives the daily rotation so every display agrees.
    public let dayOrdinal: Int

    public init(hour: Int, minute: Int, dayOrdinal: Int) {
        self.hour = hour
        self.minute = minute
        self.dayOrdinal = dayOrdinal
    }

    public init(date: Date, calendar: Calendar = .current) {
        let c = calendar.dateComponents([.hour, .minute], from: date)
        hour = c.hour ?? 0
        minute = c.minute ?? 0
        dayOrdinal = calendar.ordinality(of: .day, in: .era, for: date) ?? 0
    }

    /// Hour on a 12-hour dial (1–12).
    public var hour12: Int { hour % 12 == 0 ? 12 : hour % 12 }
    public var minuteKey: Int { hour * 60 + minute }
}

/// Deterministic generator (SplitMix64) so all screens pick the same verse for the same minute.
public struct SeededGenerator: RandomNumberGenerator, Sendable {
    private var state: UInt64

    public init(seed: UInt64) { state = seed }

    public init(time: ClockTime, salt: String) {
        var h: UInt64 = 0xcbf29ce484222325
        for byte in salt.utf8 { h = (h ^ UInt64(byte)) &* 0x100000001b3 }
        let mixed = (UInt64(bitPattern: Int64(time.dayOrdinal)) &* 0x9E3779B97F4A7C15) ^ (UInt64(time.minuteKey) << 20) ^ h
        state = mixed
    }

    public mutating func next() -> UInt64 {
        state &+= 0x9E3779B97F4A7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58476D1CE4E5B9
        z = (z ^ (z >> 27)) &* 0x94D049BB133111EB
        return z ^ (z >> 31)
    }
}

/// Chooses the verse for a given minute from a translation.
public struct VerseSelector: Sendable {
    public let translation: Translation
    public let curated: CuratedVerses

    public init(translation: Translation, curated: CuratedVerses) {
        self.translation = translation
        self.curated = curated
    }

    /// Candidate chapters for an hour, best first.
    public static func chapters(for time: ClockTime, use24Hour: Bool) -> [Int] {
        if use24Hour && time.hour >= 13 { return [time.hour, time.hour12] }
        return [time.hour12]
    }

    /// Returns nil only when the translation has no usable verse at all (never for the bundled data).
    ///
    /// `variant` numbers the display when each screen should show a different verse: variant 0 is the main
    /// display's choice, variant 1 the next screen's, and so on. Higher variants avoid the verses the lower ones
    /// picked whenever the minute offers enough candidates, and fall back to the same verse when it does not.
    public func select(time: ClockTime, preferences: Preferences, use24Hour: Bool? = nil, variant: Int = 0) -> Selection? {
        let variant = max(0, variant)
        var taken: Set<VerseRef> = []
        for v in 0..<variant {
            if let earlier = selectOne(time: time, preferences: preferences, use24Hour: use24Hour, variant: v, avoiding: taken) {
                taken.insert(earlier.primary.ref)
            }
        }
        return selectOne(time: time, preferences: preferences, use24Hour: use24Hour, variant: variant, avoiding: taken)
    }

    private func selectOne(time: ClockTime, preferences: Preferences, use24Hour: Bool?, variant: Int, avoiding taken: Set<VerseRef>) -> Selection? {
        var rng = SeededGenerator(time: time, salt: variant == 0 ? translation.id : "\(translation.id)#\(variant)")
        let twentyFour = use24Hour ?? preferences.uses24Hour()

        if time.minute == 0 {
            if let verse = hourVerse(time: time, offset: variant, rng: &rng) {
                return Selection(verses: extended(verse, enabled: preferences.extendFragments), kind: .topOfHour)
            }
        }

        for chapter in VerseSelector.chapters(for: time, use24Hour: twentyFour) {
            if let verse = pick(chapter: chapter, minute: time.minute, time: time, preferences: preferences, variant: variant, avoiding: taken, rng: &rng) {
                return Selection(verses: extended(verse, enabled: preferences.extendFragments), kind: .match)
            }
        }

        if let verse = hourVerse(time: time, offset: variant, rng: &rng) {
            return Selection(verses: extended(verse, enabled: preferences.extendFragments), kind: .fallback)
        }
        return nil
    }

    // MARK: - Internals

    private func pick(chapter: Int, minute: Int, time: ClockTime, preferences: Preferences, variant: Int,
                      avoiding taken: Set<VerseRef>, rng: inout SeededGenerator) -> Verse? {
        let candidates = translation.verses(chapter: chapter, verse: minute)
        guard !candidates.isEmpty else { return nil }

        if preferences.preferFamous, chapter == time.hour12 {
            let famous = (curated.bySlot[time.hour12 * 60 + minute] ?? []).compactMap { translation[$0] }
            if !famous.isEmpty {
                let index = (VerseSelector.famousIndex(count: famous.count, dayOrdinal: time.dayOrdinal) + variant) % famous.count
                if !taken.contains(famous[index].ref) || famous.allSatisfy({ taken.contains($0.ref) }) {
                    return famous[index]
                }
                // The shifted famous verse is already on another display; fall through to the general pool.
            }
        }

        // A verse that merely ends mid-sentence is as good as a clean one once the following verse completes it.
        let extendable: (Verse) -> Bool = { verse in
            guard preferences.extendFragments, verse.flags == [.penalize], verse.endsMidSentence else { return false }
            let chain = self.extended(verse, enabled: true)
            return chain.count > 1 && !(chain.last?.endsMidSentence ?? true)
        }
        let clean = candidates.filter { $0.flags.isEmpty || extendable($0) }
        // Genealogies and name lists (.exclude) are never shown; the caller folds to another chapter or a time verse.
        let weak = candidates.filter { !$0.flags.contains(.exclude) }
        // Verses no other display is showing come first, clean before weak; only then repeat one.
        for tier in [clean.filter { !taken.contains($0.ref) }, weak.filter { !taken.contains($0.ref) }, clean, weak] {
            if let verse = tier.randomElement(using: &rng) { return verse }
        }
        return nil
    }

    /// Which curated verse to show on a given day. The first (most famous) verse is shown two days out of three,
    /// so 3:16 is John 3:16 most of the time; the remaining days rotate through the rest of the list.
    public static func famousIndex(count: Int, dayOrdinal: Int) -> Int {
        guard count > 1 else { return 0 }
        let day = ((dayOrdinal % (3 * (count - 1))) + 3 * (count - 1)) % (3 * (count - 1))
        if day % 3 != 2 { return 0 }
        return 1 + (day / 3) % (count - 1)
    }

    private func hourVerse(time: ClockTime, offset: Int, rng: inout SeededGenerator) -> Verse? {
        let pool = curated.hourVerses.compactMap { translation[$0] }
        guard !pool.isEmpty else { return nil }
        if time.minute == 0 {
            // Walk the list hour by hour so a day cycles through most of it, offset by the day for variety
            // (and by the display number, so screens showing different verses differ here too).
            let index = ((time.dayOrdinal * 7 + time.hour + offset) % pool.count + pool.count) % pool.count
            return pool[index]
        }
        return pool.randomElement(using: &rng)
    }

    /// Append up to two following verses while the text still ends mid-sentence and stays readable in length.
    public func extended(_ verse: Verse, enabled: Bool) -> [Verse] {
        var verses = [verse]
        guard enabled else { return verses }
        var total = verse.text.count
        while verses.count < 3, let last = verses.last, last.endsMidSentence,
              let next = translation[last.ref.next], !next.text.isEmpty,
              total + next.text.count <= 420 {
            verses.append(next)
            total += next.text.count
        }
        return verses
    }
}

/// Formats references for display.
public struct ReferenceFormatter: Sendable {
    public let canon: Canon
    public var pad: Bool

    public init(canon: Canon, pad: Bool = true) {
        self.canon = canon
        self.pad = pad
    }

    /// "John 03:16", "Romans 08:38–39", "Jude 01:24".
    public func string(for selection: Selection) -> String {
        let first = selection.primary.ref
        let name = canon.book(code: first.book)?.name ?? first.book
        let chapter = number(first.chapter)
        var verses = number(first.verse)
        if let last = selection.verses.last?.ref, selection.isExtended, last.chapter == first.chapter {
            verses += "\u{2013}" + number(last.verse)
        }
        return "\(name) \(chapter):\(verses)"
    }

    private func number(_ n: Int) -> String {
        pad && n < 10 ? "0\(n)" : String(n)
    }
}
