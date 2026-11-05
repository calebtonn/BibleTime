import Foundation
#if canImport(BibleTimeCore)
import BibleTimeCore
#endif

// Cross-check between this Swift core and the Windows C++ port (Windows/tests/CrossCheck.cpp).
// Both programs take the Resources directory and must print byte-identical text: verse selection for every minute
// of several days in every bundled translation, Foundation's day ordinals, the seeded generator, character counts,
// reference parsing and clock strings. Run both with `make crosscheck`, which diffs the outputs.
//
// Keep the two files in lockstep: same sections, same loops in the same order, same formatting.

let resourcesPath = CommandLine.arguments.dropFirst().first ?? "Resources"
let resources = URL(fileURLWithPath: resourcesPath)

let clock: VerseClock
do {
    clock = try VerseClock(resources: resources, importDirectories: [])
} catch {
    FileHandle.standardError.write(Data("crosscheck: could not load resources at \(resources.path): \(error)\n".utf8))
    exit(1)
}

// MARK: - Output helpers

var out = ""
out.reserveCapacity(1 << 21)

func flush() {
    FileHandle.standardOutput.write(Data(out.utf8))
    out = ""
}

func emit(_ line: String) {
    out += line
    out += "\n"
    if out.utf8.count >= 1 << 20 { flush() }
}

func pad(_ n: Int, _ width: Int) -> String {
    let s = String(n)
    return s.count >= width ? s : String(repeating: "0", count: width - s.count) + s
}

func hex16(_ v: UInt64) -> String {
    let s = String(v, radix: 16)
    return String(repeating: "0", count: 16 - s.count) + s
}

func fnv1a(_ s: String) -> UInt64 {
    var h: UInt64 = 0xcbf29ce484222325
    for byte in s.utf8 { h = (h ^ UInt64(byte)) &* 0x100000001b3 }
    return h
}

func kindLetter(_ kind: SelectionKind) -> String {
    switch kind {
    case .match: return "M"
    case .topOfHour: return "T"
    case .fallback: return "F"
    }
}

/// Proleptic Gregorian civil date <-> days since 1970-01-01 (Howard Hinnant's algorithms). Used only to enumerate
/// dates identically on both sides; the ordinal itself comes from Foundation here and from dayOrdinalFor in C++.
func daysFromCivil(_ year: Int, _ month: Int, _ day: Int) -> Int {
    let y = month <= 2 ? year - 1 : year
    let era = (y >= 0 ? y : y - 399) / 400
    let yoe = y - era * 400
    let doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy
    return era * 146097 + doe - 719468
}

func civilFromDays(_ days: Int) -> (year: Int, month: Int, day: Int) {
    let z = days + 719468
    let era = (z >= 0 ? z : z - 146096) / 146097
    let doe = z - era * 146097
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
    let mp = (5 * doy + 2) / 153
    let d = doy - (153 * mp + 2) / 5 + 1
    let m = mp < 10 ? mp + 3 : mp - 9
    return (yoe + era * 400 + (m <= 2 ? 1 : 0), m, d)
}

func dateString(_ y: Int, _ m: Int, _ d: Int) -> String { "\(pad(y, 4))-\(pad(m, 2))-\(pad(d, 2))" }

var utc = Calendar(identifier: .gregorian)
utc.timeZone = TimeZone(identifier: "UTC")!
let enUS = Locale(identifier: "en_US")

func utcDate(_ y: Int, _ m: Int, _ d: Int, _ h: Int, _ mi: Int) -> Date? {
    utc.date(from: DateComponents(year: y, month: m, day: d, hour: h, minute: mi))
}

func loadTranslation(_ id: String) -> Translation {
    guard let t = try? clock.catalog.translation(id: id), t.id == id else {
        FileHandle.standardError.write(Data("crosscheck: translation \(id) did not load\n".utf8))
        exit(1)
    }
    return t
}

let translationIDs = ["kjv", "web", "asv"]

emit("# BibleTime cross-check 1")

// MARK: - (a) Selection

do {
    var combos: [(name: String, prefs: Preferences)] = []
    combos.append((name: "default", prefs: Preferences()))
    var p = Preferences()
    p.preferFamous = false
    combos.append((name: "noFamous", prefs: p))
    p = Preferences()
    p.extendFragments = false
    combos.append((name: "noExtend", prefs: p))
    p = Preferences()
    p.padReference = false
    combos.append((name: "noPad", prefs: p))
    let days = [0, 1, 2, 3, 100, 739925, 740288, -5]

    for id in translationIDs {
        let translation = loadTranslation(id)
        let selector = VerseSelector(translation: translation, curated: clock.curated)
        for (index, combo) in combos.enumerated() {
            let formatter = ReferenceFormatter(canon: clock.canon, pad: combo.prefs.padReference)
            // Display variants only for the default preferences, to bound the output size.
            let variants = index == 0 ? [0, 1, 2] : [0]
            for use24 in [false, true] {
                emit("## selection \(id) \(combo.name) \(use24 ? "24h" : "12h")")
                for day in days {
                    emit("# day \(day)")
                    for minuteKey in 0..<1440 {
                        let time = ClockTime(hour: minuteKey / 60, minute: minuteKey % 60, dayOrdinal: day)
                        for variant in variants {
                            let prefix = "\(pad(time.hour, 2)):\(pad(time.minute, 2)) \(variant) "
                            guard let s = selector.select(time: time, preferences: combo.prefs, use24Hour: use24, variant: variant) else {
                                emit(prefix + "nil")
                                continue
                            }
                            let refs = s.verses.map { $0.ref.description }.joined(separator: " ")
                            emit(prefix + "\(kindLetter(s.kind)) \(formatter.string(for: s))|\(refs)|\(hex16(fnv1a(s.text)))")
                        }
                    }
                }
            }
        }
    }
}

// MARK: - (b) Day ordinals (Foundation's Gregorian calendar, UTC, at noon)

do {
    emit("## day ordinals")
    var dates: [(Int, Int, Int)] = []
    var z = daysFromCivil(1600, 1, 1)
    let last = daysFromCivil(2200, 12, 31)
    while z <= last {
        let c = civilFromDays(z)
        dates.append((c.year, c.month, c.day))
        z += 23
    }
    dates += [(1582, 10, 1), (1582, 10, 4), (1582, 10, 15), (1582, 10, 16), (1970, 1, 1), (2000, 2, 28), (2000, 2, 29),
              (2000, 3, 1), (2026, 11, 3), (2026, 11, 5), (2100, 3, 1)]
    for (y, m, d) in dates {
        var value = "nil"
        if let date = utcDate(y, m, d, 12, 0), let ordinal = utc.ordinality(of: .day, in: .era, for: date) {
            value = String(ordinal)
        }
        emit("\(dateString(y, m, d)) \(value)")
    }
}

// MARK: - (c) Seeded generator and Swift's next(upperBound:)

do {
    emit("## generator")
    let seeds: [UInt64] = [0, 1, 42, 123456789, 0x9E3779B97F4A7C15, UInt64.max]
    let bounds: [UInt64] = [1, 2, 3, 5, 6, 7, 10, 100, 1000003, 4294967297, 9223372036854775808, 9223372036854775809, UInt64.max]
    for seed in seeds {
        var g = SeededGenerator(seed: seed)
        let n1 = g.next(), n2 = g.next(), n3 = g.next(), n4 = g.next()
        emit("seed \(seed) next \(n1) \(n2) \(n3) \(n4)")
        for bound in bounds {
            let a = UInt64.random(in: 0..<bound, using: &g)
            let b = UInt64.random(in: 0..<bound, using: &g)
            let c = UInt64.random(in: 0..<bound, using: &g)
            emit("bound \(bound) \(a) \(b) \(c)")
        }
        for count in [1, 2, 3, 17, 26, 1000] {
            let items = Array(0..<count)
            let a = items.randomElement(using: &g)!
            let b = items.randomElement(using: &g)!
            let c = items.randomElement(using: &g)!
            emit("element \(count) \(a) \(b) \(c)")
        }
        let empty: [Int] = []
        let none = empty.randomElement(using: &g)
        let after = g.next()
        emit("empty \(none == nil ? "nil" : "value") then \(after)")
    }
    let salts = ["kjv", "web#1", "asv#2", "", "\u{E9}t\u{E9}", "test-1"]
    let times = [
        ClockTime(hour: 0, minute: 0, dayOrdinal: 0),
        ClockTime(hour: 15, minute: 16, dayOrdinal: 739924),
        ClockTime(hour: 23, minute: 59, dayOrdinal: -5),
        ClockTime(hour: 12, minute: 30, dayOrdinal: Int(Int64.max)),
        ClockTime(hour: 1, minute: 1, dayOrdinal: Int(Int64.min)),
    ]
    for (ti, time) in times.enumerated() {
        for (si, salt) in salts.enumerated() {
            var g = SeededGenerator(time: time, salt: salt)
            let a = g.next(), b = g.next()
            emit("time \(ti) salt \(si) \(a) \(b)")
        }
    }
}

// MARK: - (d) Character counts (String.count) against Unicode scalar counts

do {
    for id in translationIDs {
        let translation = loadTranslation(id)
        var verses = 0, characters = 0, scalars = 0
        var differing: [String] = []
        for book in clock.canon.books {
            for chapter in 1...max(book.chapters, 1) {
                for number in 1...200 {
                    guard let verse = translation[VerseRef(book: book.code, chapter: chapter, verse: number)] else { continue }
                    verses += 1
                    let c = verse.text.count
                    let s = verse.text.unicodeScalars.count
                    characters += c
                    scalars += s
                    if c != s { differing.append("\(verse.ref.description) \(c) \(s)") }
                }
            }
        }
        emit("## characters \(id) verses \(verses) characters \(characters) scalars \(scalars)")
        for line in differing { emit(line) }
    }
}

// MARK: - (e) Reference parsing

do {
    emit("## parse")
    let inputs = [
        "John 3:16", "1 John 4:8", "Psalms 23:1", "Psalm 119:105", "Song of Solomon 2:4", "song of songs 2:4", "Canticles 1:1",
        "Revelation 22:21", "Revelations 1:1", "Apocalypse 1:1", "I Samuel 3:10", "II Kings 2:11", "iii john 1:2", "1st John 1:9",
        "Gen 1:1", "GEN 1:1", "jhn 3:16", "1 Cor. 13:4", "Phil 4:13", "  Romans 8:28  ", "Romans  8:28", "Jude 1:24", "John 3:+16",
        "John 3:-1", "John 3:16a", "John :16", "John 3:", "John", "Nowhere 1:1", "", ":", "3:16", "John 3:16:17",
        "Matthew 99999999999999999999:1", "Ps. 23:1", "Song 2:4", "1 Jn 4:8", "1 Thess 5:17", "Phlm 1:6", "\tEccl 3:1\t",
    ]
    for input in inputs {
        let parsed = clock.canon.parse(reference: input).map { $0.description } ?? "nil"
        emit("[\(input)] \(parsed)")
    }
}

// MARK: - (f) Clock strings and the facade

do {
    emit("## clock")
    for minuteKey in 0..<1440 {
        guard let date = utcDate(2026, 11, 5, minuteKey / 60, minuteKey % 60) else {
            emit("nil")
            continue
        }
        let twelve = VerseClock.clockString(for: date, use24Hour: false, calendar: utc, locale: enUS)
        let twentyFour = VerseClock.clockString(for: date, use24Hour: true, calendar: utc, locale: enUS)
        emit("\(twelve)|\(twentyFour)")
    }

    emit("## facade")
    var prefs = Preferences()
    prefs.hourStyle = .twelveHour
    let start = daysFromCivil(2026, 1, 1)
    for step in 0..<53 {
        let c = civilFromDays(start + step * 7)
        for (h, m) in [(15, 16), (0, 0), (8, 38), (21, 7)] {
            guard let date = utcDate(c.year, c.month, c.day, h, m),
                  let content = try? clock.content(for: date, preferences: prefs, calendar: utc, locale: enUS) else {
                emit("\(dateString(c.year, c.month, c.day)) nil")
                continue
            }
            emit("\(dateString(c.year, c.month, c.day)) \(content.minuteKey) \(content.clock) \(kindLetter(content.kind)) \(content.reference) \(content.translation) \(hex16(fnv1a(content.text)))")
        }
    }
}

emit("# end")
flush()
