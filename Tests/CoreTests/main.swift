import Foundation
#if canImport(BibleTimeCore)
import BibleTimeCore
#endif

// A dependency-free test runner: XCTest is not available with Command Line Tools alone.
// Run with `make test` or `swift run CoreTests Resources`.

var failures = 0
var passes = 0

func check(_ condition: @autoclosure () -> Bool, _ message: String, file: String = #file, line: Int = #line) {
    if condition() {
        passes += 1
    } else {
        failures += 1
        print("FAIL \(URL(fileURLWithPath: file).lastPathComponent):\(line): \(message)")
    }
}

func section(_ name: String) { print("— \(name)") }

let resourcesPath = CommandLine.arguments.dropFirst().first ?? "Resources"
let resources = URL(fileURLWithPath: resourcesPath)

let clock: VerseClock
do {
    clock = try VerseClock(resources: resources, importDirectories: [])
} catch {
    print("FAIL could not load resources at \(resources.path): \(error)")
    exit(1)
}
let canon = clock.canon
let utc = { () -> Calendar in var c = Calendar(identifier: .gregorian); c.timeZone = TimeZone(identifier: "UTC")!; return c }()
let us = Locale(identifier: "en_US")
let de = Locale(identifier: "de_DE")

func date(_ y: Int, _ mo: Int, _ d: Int, _ h: Int, _ mi: Int) -> Date {
    utc.date(from: DateComponents(year: y, month: mo, day: d, hour: h, minute: mi))!
}

section("canon")
check(canon.books.count == 66, "66 books, got \(canon.books.count)")
check(canon.books.first?.code == "GEN" && canon.books.last?.code == "REV", "Genesis first, Revelation last")
check(canon.book(code: "PSA")?.name == "Psalm", "Psalms displays as 'Psalm'")
check(canon.parse(reference: "1 John 4:8") == VerseRef(book: "1JN", chapter: 4, verse: 8), "parse '1 John 4:8'")
check(canon.parse(reference: "Psalms 23:1") == VerseRef(book: "PSA", chapter: 23, verse: 1), "parse 'Psalms 23:1' alias")
check(canon.parse(reference: "Song of Solomon 2:4")?.book == "SNG", "parse 'Song of Solomon'")
check(canon.parse(reference: "Nowhere 1:1") == nil, "unknown book is nil")
check(canon.parse(reference: "John") == nil, "missing numbers is nil")

section("translations")
check(clock.catalog.available.map(\.id) == ["kjv", "dby"], "kjv, dby available: \(clock.catalog.available.map(\.id))")
var translations: [Translation] = []
for info in clock.catalog.available {
    do {
        if let t = try clock.catalog.translation(id: info.id) { translations.append(t) }
    } catch {
        check(false, "load \(info.id): \(error)")
    }
}
check(translations.count == 2, "loaded 2 translations")
for t in translations {
    check(t.verseCount > 20_000, "\(t.id) has \(t.verseCount) verses")
    let john = t[VerseRef(book: "JHN", chapter: 3, verse: 16)]
    check(john?.text.hasPrefix("For God so loved the world") == true, "\(t.id) John 3:16: \(john?.text ?? "nil")")
    check(john?.flags.isEmpty == true, "\(t.id) John 3:16 unflagged")
    let psalm = t[VerseRef(book: "PSA", chapter: 23, verse: 1)]
    check(psalm?.text.contains("A Psalm") == false, "\(t.id) Psalm 23:1 has no superscription: \(psalm?.text ?? "nil")")
    check(t.verses(chapter: 3, verse: 16).contains { $0.ref.book == "JHN" }, "\(t.id) slot 3:16 includes John")
    check(t.verses(chapter: 3, verse: 16).count > 20, "\(t.id) slot 3:16 has many candidates: \(t.verses(chapter: 3, verse: 16).count)")
    check(t.verses(chapter: 0, verse: 16).isEmpty && t.verses(chapter: 3, verse: 0).isEmpty, "\(t.id) no chapter/verse zero")
    let ordered = t.verses(chapter: 1, verse: 1).map(\.ref.book)
    check(ordered.first == "GEN" && ordered.last == "REV", "\(t.id) slot results in canonical order")
    check(t.source?.license == "Public Domain", "\(t.id) marked public domain")
}
if let kjv = translations.first(where: { $0.id == "kjv" }) {
    check(kjv[VerseRef(book: "MAT", chapter: 1, verse: 2)]?.flags.contains(.exclude) == true, "KJV Matthew 1:2 (genealogy) excluded")
    check(kjv[VerseRef(book: "EXO", chapter: 1, verse: 2)]?.flags.contains(.exclude) == true, "KJV Exodus 1:2 (name list) excluded")
    check(kjv[VerseRef(book: "JHN", chapter: 11, verse: 35)]?.text == "Jesus wept.", "KJV John 11:35")
    check(kjv[VerseRef(book: "JHN", chapter: 11, verse: 35)]?.flags.isEmpty == true, "KJV 'Jesus wept.' is whitelisted despite being short")
    check(kjv[VerseRef(book: "ROM", chapter: 8, verse: 38)]?.endsMidSentence == true, "KJV Romans 8:38 ends mid-sentence")
    check(kjv[VerseRef(book: "PSA", chapter: 90, verse: 12)] != nil, "KJV includes hour verse Psalm 90:12 beyond chapter 23")
    check(kjv[VerseRef(book: "GEN", chapter: 1, verse: 1)]?.text == "In the beginning God created the heaven and the earth.", "KJV Genesis 1:1")
    for v in [kjv[VerseRef(book: "JHN", chapter: 3, verse: 16)]!, kjv[VerseRef(book: "PSA", chapter: 23, verse: 1)]!] {
        check(!v.text.contains("¶") && !v.text.contains("<"), "no markup in \(v.ref)")
    }
}

section("curated")
check(clock.curated.bySlot[3 * 60 + 16]?.first == VerseRef(book: "JHN", chapter: 3, verse: 16), "3:16 → John 3:16 first")
check(clock.curated.bySlot[4 * 60 + 13]?.contains(VerseRef(book: "PHP", chapter: 4, verse: 13)) == true, "4:13 includes Philippians 4:13")
check(clock.curated.hourVerses.count >= 12, "at least 12 hour verses, got \(clock.curated.hourVerses.count)")
check(clock.curated.bySlot.count > 150, "curated covers \(clock.curated.bySlot.count) slots")
for t in translations {
    for refs in clock.curated.bySlot.values { for r in refs { check(t[r] != nil, "\(t.id) has curated \(r)") } }
    for r in clock.curated.hourVerses { check(t[r] != nil, "\(t.id) has hour verse \(r)") }
}

section("selection")
var prefs = Preferences()
prefs.hourStyle = .twelveHour
let kjv = translations.first { $0.id == "kjv" }!
let selector = VerseSelector(translation: kjv, curated: clock.curated)
let formatter = ReferenceFormatter(canon: canon)

let threeSixteen = selector.select(time: ClockTime(hour: 15, minute: 16, dayOrdinal: 99), preferences: prefs, use24Hour: false)
check(threeSixteen?.primary.ref == VerseRef(book: "JHN", chapter: 3, verse: 16), "15:16 shows John 3:16: \(String(describing: threeSixteen?.primary.ref))")
check(threeSixteen?.kind == .match, "15:16 is a match")
check(threeSixteen.map(formatter.string) == "John 03:16", "reference padded: \(threeSixteen.map(formatter.string) ?? "nil")")
check(ReferenceFormatter(canon: canon, pad: false).string(for: threeSixteen!) == "John 3:16", "reference unpadded")

let am = selector.select(time: ClockTime(hour: 3, minute: 16, dayOrdinal: 99), preferences: prefs, use24Hour: false)
check(am?.primary.ref == threeSixteen?.primary.ref, "3:16 AM and PM agree")

var johnDays = 0, otherFamous = Set<VerseRef>()
for day in 0..<90 {
    if let s = selector.select(time: ClockTime(hour: 3, minute: 16, dayOrdinal: day), preferences: prefs, use24Hour: false) {
        if s.primary.ref == VerseRef(book: "JHN", chapter: 3, verse: 16) { johnDays += 1 } else { otherFamous.insert(s.primary.ref) }
        check(clock.curated.bySlot[3 * 60 + 16]?.contains(s.primary.ref) == true, "day \(day) 3:16 is curated: \(s.primary.ref)")
    }
}
check(johnDays == 60, "John 3:16 shows two days in three (\(johnDays)/90)")
check(otherFamous.count == (clock.curated.bySlot[3 * 60 + 16]?.count ?? 1) - 1, "every other famous 3:16 verse appears on the remaining days: \(otherFamous)")
check(VerseSelector.famousIndex(count: 1, dayOrdinal: 5) == 0, "single curated verse always index 0")
check(VerseSelector.famousIndex(count: 3, dayOrdinal: -7) >= 0, "negative day ordinals are safe")
check((0..<12).map { VerseSelector.famousIndex(count: 3, dayOrdinal: $0) } == [0, 0, 1, 0, 0, 2, 0, 0, 1, 0, 0, 2], "famous index pattern")

var random = prefs
random.preferFamous = false
let a = selector.select(time: ClockTime(hour: 3, minute: 16, dayOrdinal: 100), preferences: random, use24Hour: false)
let b = selector.select(time: ClockTime(hour: 3, minute: 16, dayOrdinal: 100), preferences: random, use24Hour: false)
check(a == b, "selection is deterministic for the same day")
check(a?.primary.ref.chapter == 3 && a?.primary.ref.verse == 16, "random pick still matches 3:16")
var differentDays = Set<VerseRef>()
for day in 0..<30 {
    if let s = selector.select(time: ClockTime(hour: 3, minute: 16, dayOrdinal: day), preferences: random, use24Hour: false) { differentDays.insert(s.primary.ref) }
}
check(differentDays.count > 5, "random mode varies across days (\(differentDays.count) distinct in 30 days)")

let top = selector.select(time: ClockTime(hour: 9, minute: 0, dayOrdinal: 100), preferences: prefs, use24Hour: false)
check(top?.kind == .topOfHour, "minute 0 shows an hour verse")
check(top.map { clock.curated.hourVerses.contains($0.primary.ref) } == true, "hour verse comes from the pool")
var hours = Set<VerseRef>()
for h in 0..<24 { if let s = selector.select(time: ClockTime(hour: h, minute: 0, dayOrdinal: 100), preferences: prefs, use24Hour: false) { hours.insert(s.primary.ref) } }
check(hours.count >= 12, "top-of-hour verses rotate through the day (\(hours.count) distinct)")

let empty = selector.select(time: ClockTime(hour: 4, minute: 57, dayOrdinal: 100), preferences: prefs, use24Hour: false)
check(empty?.kind == .fallback, "4:57 (no chapter 4 verse 57 in the Bible) falls back")
check(empty?.text.isEmpty == false, "fallback has text")

let twentyFour = selector.select(time: ClockTime(hour: 15, minute: 16, dayOrdinal: 100), preferences: prefs, use24Hour: true)
check(twentyFour?.primary.ref.chapter == 15 && twentyFour?.primary.ref.verse == 16, "24-hour 15:16 prefers chapter 15: \(String(describing: twentyFour?.primary.ref))")
let lateFold = selector.select(time: ClockTime(hour: 23, minute: 58, dayOrdinal: 100), preferences: prefs, use24Hour: true)
check(lateFold?.kind == .fallback, "23:58 has no chapter 23 or 11 verse 58 and falls back: \(String(describing: lateFold?.primary.ref))")
let fold = selector.select(time: ClockTime(hour: 18, minute: 51, dayOrdinal: 100), preferences: prefs, use24Hour: true)
check(fold?.kind == .match && fold?.primary.ref.chapter == 6 && fold?.primary.ref.verse == 51, "18:51 folds to chapter 6: \(String(describing: fold?.primary.ref))")
let excludedOnly = selector.select(time: ClockTime(hour: 2, minute: 55, dayOrdinal: 100), preferences: prefs, use24Hour: false)
check(excludedOnly?.kind == .fallback, "2:55 (only census verses) falls back to a time verse instead of a list: \(String(describing: excludedOnly?.primary.ref))")
for h in 0..<24 { for m in 0..<60 {
    if let s = selector.select(time: ClockTime(hour: h, minute: m, dayOrdinal: 7), preferences: prefs, use24Hour: false) {
        check(!s.primary.flags.contains(.exclude), "\(h):\(m) never shows an excluded verse: \(s.primary.ref)")
    }
} }
let midnight = selector.select(time: ClockTime(hour: 0, minute: 16, dayOrdinal: 100), preferences: prefs, use24Hour: true)
check(midnight?.primary.ref.chapter == 12, "00:16 uses chapter 12")

var extended = prefs
extended.extendFragments = true
var sawJude = false
for day in 0..<90 {
    if let s = selector.select(time: ClockTime(hour: 1, minute: 24, dayOrdinal: day), preferences: extended, use24Hour: false),
       s.primary.ref == VerseRef(book: "JUD", chapter: 1, verse: 24) {
        sawJude = true
        check(s.isExtended && s.verses.count == 2 && s.verses[1].ref == VerseRef(book: "JUD", chapter: 1, verse: 25), "Jude 1:24 is extended to 1:25: \(s.verses.map(\.ref))")
        check(formatter.string(for: s) == "Jude 01:24\u{2013}25", "extended reference: \(formatter.string(for: s))")
        check(s.text.hasSuffix("Amen."), "extended text completes the sentence: \(s.text.suffix(30))")
        break
    }
}
check(sawJude, "Jude 1:24 appears among the curated 1:24 verses")
var noExtend = extended
noExtend.extendFragments = false
for day in 0..<90 {
    if let s = selector.select(time: ClockTime(hour: 1, minute: 24, dayOrdinal: day), preferences: noExtend, use24Hour: false) {
        check(!s.isExtended, "no extension when disabled")
    }
}
var fragmentsChosen = 0
for day in 0..<60 {
    if let s = selector.select(time: ClockTime(hour: 8, minute: 38, dayOrdinal: day), preferences: random, use24Hour: false), s.isExtended { fragmentsChosen += 1 }
}
check(fragmentsChosen > 0, "random mode also uses extendable fragments (\(fragmentsChosen)/60 days at 8:38)")
let randomNoExtend: Preferences = { var p = random; p.extendFragments = false; return p }()
for day in 0..<60 {
    if let s = selector.select(time: ClockTime(hour: 8, minute: 38, dayOrdinal: day), preferences: randomNoExtend, use24Hour: false) {
        check(!s.primary.endsMidSentence || s.primary.flags.isEmpty, "without extension, fragments are avoided: \(s.primary.ref)")
    }
}

section("display variants")
do {
    let slot = ClockTime(hour: 3, minute: 16, dayOrdinal: 99)
    let v0 = selector.select(time: slot, preferences: prefs, use24Hour: false)!
    let v1 = selector.select(time: slot, preferences: prefs, use24Hour: false, variant: 1)!
    let v2 = selector.select(time: slot, preferences: prefs, use24Hour: false, variant: 2)!
    check(v0.primary.ref == VerseRef(book: "JHN", chapter: 3, verse: 16), "variant 0 is the main display's verse")
    check(v1.primary.ref != v0.primary.ref && v2.primary.ref != v0.primary.ref && v1.primary.ref != v2.primary.ref, "three displays show three different 3:16 verses: \(v0.primary.ref) \(v1.primary.ref) \(v2.primary.ref)")
    check([v0, v1, v2].allSatisfy { $0.primary.ref.chapter == 3 && $0.primary.ref.verse == 16 && $0.kind == .match }, "all variants still match 3:16")
    check(selector.select(time: slot, preferences: prefs, use24Hour: false, variant: 1) == v1, "variants are deterministic")
    check(selector.select(time: slot, preferences: prefs, use24Hour: false, variant: -3)?.primary.ref == v0.primary.ref, "negative variant is the main display")
    // Random mode: a secondary display avoids the main display's verse too.
    var collisions = 0, total = 0
    for h in 1...12 { for m in 1...59 {
        let t = ClockTime(hour: h, minute: m, dayOrdinal: 7)
        guard let a = selector.select(time: t, preferences: random, use24Hour: false), a.kind == .match,
              let b = selector.select(time: t, preferences: random, use24Hour: false, variant: 1) else { continue }
        total += 1
        let pool = kjv.verses(chapter: h, verse: m).filter { !$0.flags.contains(.exclude) }
        if a.primary.ref == b.primary.ref { collisions += 1; check(pool.count == 1, "\(h):\(m) displays coincide only when a single verse exists (pool \(pool.count))") }
    } }
    check(total > 600 && collisions < total / 10, "secondary display differs on most minutes (\(collisions) coincidences of \(total))")
    // Top of the hour: the time verse shifts too.
    let top0 = selector.select(time: ClockTime(hour: 9, minute: 0, dayOrdinal: 100), preferences: prefs, use24Hour: false)!
    let top1 = selector.select(time: ClockTime(hour: 9, minute: 0, dayOrdinal: 100), preferences: prefs, use24Hour: false, variant: 1)!
    check(top0.kind == .topOfHour && top1.kind == .topOfHour && top0.primary.ref != top1.primary.ref, "top-of-hour verses differ per display")
    let c1 = try clock.content(for: date(2026, 11, 3, 15, 16), preferences: prefs, calendar: utc, locale: us, variant: 1)
    check(c1.reference != "John 03:16" && c1.reference.hasSuffix("03:16"), "facade passes the variant through: \(c1.reference)")
} catch { check(false, "display variants threw \(error)") }

section("coverage")
for t in translations {
    let sel = VerseSelector(translation: t, curated: clock.curated)
    for use24 in [false, true] {
        var missing = 0
        var kinds: [SelectionKind: Int] = [:]
        for h in 0..<24 {
            for m in 0..<60 {
                guard let s = sel.select(time: ClockTime(hour: h, minute: m, dayOrdinal: 42), preferences: prefs, use24Hour: use24) else { missing += 1; continue }
                kinds[s.kind, default: 0] += 1
                if s.text.isEmpty { missing += 1 }
                if s.kind == .match {
                    let chapters = VerseSelector.chapters(for: ClockTime(hour: h, minute: m, dayOrdinal: 42), use24Hour: use24)
                    check(chapters.contains(s.primary.ref.chapter) && s.primary.ref.verse == m, "\(t.id) \(h):\(m) match is \(s.primary.ref)")
                }
            }
        }
        check(missing == 0, "\(t.id) \(use24 ? "24h" : "12h"): every minute has text (missing \(missing))")
        if !use24 {
            var fallbackSlots: [String] = []
            for h in 1...12 { for m in 1...59 {
                if sel.select(time: ClockTime(hour: h, minute: m, dayOrdinal: 42), preferences: prefs, use24Hour: false)?.kind == .fallback { fallbackSlots.append("\(h):\(String(format: "%02d", m))") }
            } }
            print("   \(t.id) 12h slots without a verse (\(fallbackSlots.count)): \(fallbackSlots.joined(separator: " "))")
        }
        check((kinds[.match] ?? 0) >= 1300, "\(t.id) \(use24 ? "24h" : "12h"): \(kinds[.match] ?? 0) matched minutes, \(kinds[.topOfHour] ?? 0) top-of-hour, \(kinds[.fallback] ?? 0) fallback")
    }
}

section("verse clock facade")
prefs.hourStyle = .twelveHour
do {
    let content = try clock.content(for: date(2026, 11, 3, 15, 16), preferences: prefs, calendar: utc, locale: us)
    check(content.reference == "John 03:16", "facade reference: \(content.reference)")
    check(content.clock == "3:16 PM", "12h clock string: \(content.clock)")
    check(content.translation == "KJV", "translation abbreviation")
    prefs.hourStyle = .twentyFourHour
    let c24 = try clock.content(for: date(2026, 11, 3, 15, 16), preferences: prefs, calendar: utc, locale: us)
    check(c24.clock == "15:16", "24h clock string: \(c24.clock)")
    check(c24.reference.hasSuffix("15:16"), "24h reference: \(c24.reference)")
    prefs.hourStyle = .twelveHour
    prefs.translation = "dby"
    let darby = try clock.content(for: date(2026, 11, 3, 15, 16), preferences: prefs, calendar: utc, locale: us)
    check(darby.translation == "DBY" && darby.reference == "John 03:16" && darby.text.contains("whosoever believes on him"), "Darby wording: \(darby.reference) \(darby.text)")
    prefs.translation = "nope"
    let fallback = try clock.content(for: date(2026, 11, 3, 15, 16), preferences: prefs, calendar: utc, locale: us)
    check(fallback.translation == "KJV", "unknown translation falls back to first available")
} catch {
    check(false, "facade threw \(error)")
}

section("preferences")
let defaults = Preferences()
check(Preferences(dictionary: defaults.dictionary) == defaults, "dictionary round-trip")
check(Preferences(dictionary: ["textScale": 9.0]).textScale == Preferences.textScaleRange.upperBound, "textScale clamped high")
check(Preferences(dictionary: ["textScale": 0.1]).textScale == Preferences.textScaleRange.lowerBound, "textScale clamped low")
check(Preferences(dictionary: ["layout": "bogus", "theme": "night"]).layout == .clock, "bad enum keeps default")
check(Preferences(dictionary: ["layout": "bogus", "theme": "night"]).theme == .night, "good enum applies")
check(Preferences().displayMode == .sameVerse, "default display mode shows the same verse everywhere")
check(Preferences(dictionary: ["displayMode": "differentVerses"]).displayMode == .differentVerses, "display mode decodes")
check(Preferences(dictionary: ["displayMode": "nope"]).displayMode == .sameVerse, "bad display mode keeps default")
check(!Preferences.localeUses24Hour(us), "en_US is 12-hour")
check(Preferences.localeUses24Hour(de), "de_DE is 24-hour")
var p = Preferences(); p.hourStyle = .twentyFourHour
check(p.uses24Hour(locale: us), "explicit 24h overrides locale")
p.hourStyle = .twelveHour
check(!p.uses24Hour(locale: de), "explicit 12h overrides locale")
let encoded = try! JSONEncoder().encode(defaults)
check((try? JSONDecoder().decode(Preferences.self, from: encoded)) == defaults, "Codable round-trip")

section("import")
let importRoot = FileManager.default.temporaryDirectory.appendingPathComponent("BibleTimeTests-\(ProcessInfo.processInfo.processIdentifier)")
let importDir = importRoot.appendingPathComponent("Translations")
let dropDir = importRoot.appendingPathComponent("Manual")
try? FileManager.default.removeItem(at: importRoot)
try! FileManager.default.createDirectory(at: dropDir, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: importRoot) }

func fakeTranslation(id: String, name: String = "Test Version", abbreviation: String = "TEST", mutate: ((inout [String: Any]) -> Void)? = nil) -> Data {
    var object = try! JSONSerialization.jsonObject(with: Data(contentsOf: resources.appendingPathComponent("Translations/kjv.json"))) as! [String: Any]
    object["id"] = id
    object["name"] = name
    object["abbreviation"] = abbreviation
    mutate?(&object)
    return try! JSONSerialization.data(withJSONObject: object)
}

do {
    let importing = try VerseClock(resources: resources, importDirectories: [importDir, dropDir])
    check(importing.catalog.available.map(\.id) == ["kjv", "dby"], "no imports yet")
    guard let importer = importing.importer else { check(false, "importer available"); exit(1) }
    check(importer.destination == importDir, "imports go to the first directory")

    let source = importRoot.appendingPathComponent("my-translation.json")
    try fakeTranslation(id: "Test-1").write(to: source)
    let info = try importer.importFile(at: source)
    check(info.id == "test-1" && info.isImported && info.url == importDir.appendingPathComponent("test-1.json"), "imported as test-1.json: \(info)")
    importing.catalog.rescan()
    check(importing.catalog.available.map(\.id) == ["kjv", "dby", "test-1"], "catalog lists the import: \(importing.catalog.available.map(\.id))")
    check(importing.catalog.info(id: "test-1")?.displayName == "Test Version (TEST)", "display name")
    var p = Preferences(); p.translation = "test-1"; p.hourStyle = .twelveHour
    let content = try importing.content(for: date(2026, 11, 3, 15, 16), preferences: p, calendar: utc, locale: us)
    check(content.translation == "TEST" && content.reference == "John 03:16", "imported translation is used: \(content.translation) \(content.reference)")
    let header = try TranslationHeader(url: info.url)
    check(header == TranslationHeader(id: "test-1", name: "Test Version", abbreviation: "TEST"), "header read from the file head: \(header)")

    // Manual drop directory and precedence of the first directory for duplicate ids.
    try fakeTranslation(id: "dropped", name: "Dropped", abbreviation: "DRP").write(to: dropDir.appendingPathComponent("anything.json"))
    try fakeTranslation(id: "test-1", name: "Shadowed").write(to: dropDir.appendingPathComponent("shadow.json"))
    importing.catalog.rescan()
    check(importing.catalog.available.map(\.id) == ["kjv", "dby", "dropped", "test-1"], "manual drop folder is scanned and the first directory wins: \(importing.catalog.available.map(\.id))")
    check(importing.catalog.info(id: "test-1")?.name == "Test Version", "duplicate id in a later directory is ignored")

    // Re-import replaces in place.
    try fakeTranslation(id: "test-1", name: "Test Version 2").write(to: source)
    _ = try importer.importFile(at: source)
    importing.catalog.rescan()
    check(importing.catalog.info(id: "test-1")?.name == "Test Version 2", "re-import replaces the file")
    let reloaded = try importing.catalog.translation(id: "test-1")
    check(reloaded?.name == "Test Version 2", "cache is invalidated on rescan")

    // Rejections.
    func rejects(_ data: Data, _ label: String, _ test: (TranslationImporter.ImportError) -> Bool) {
        do { _ = try importer.validate(data: data); check(false, "\(label) should be rejected") }
        catch let e as TranslationImporter.ImportError { check(test(e), "\(label): \(e)") }
        catch { check(false, "\(label): unexpected \(error)") }
    }
    rejects(fakeTranslation(id: "kjv"), "bundled id") { if case .bundledID = $0 { return true } else { return false } }
    rejects(fakeTranslation(id: "bad id!"), "bad id") { if case .invalidFormat = $0 { return true } else { return false } }
    rejects(Data("{\"hello\": 1}".utf8), "not a translation") { if case .invalidFormat = $0 { return true } else { return false } }
    rejects(Data("not json".utf8), "not json") { if case .invalidFormat = $0 { return true } else { return false } }
    rejects(fakeTranslation(id: "tiny") { $0["verses"] = Array(($0["verses"] as! [Any]).prefix(100)) }, "too few verses") { if case .tooFewVerses = $0 { return true } else { return false } }
    rejects(fakeTranslation(id: "books") { $0["verses"] = ($0["verses"] as! [[Any]]).map { row in var r = row; if (r[0] as! String) == "JHN" { r[0] = "XYZ" }; return r } }, "unknown book code") { if case .unknownBooks(let codes) = $0 { return codes == ["XYZ"] } else { return false } }
    check(FileManager.default.fileExists(atPath: info.url.path), "rejected imports leave existing files alone")

    // Removal, and the bundled ones are protected.
    try FileManager.default.removeItem(at: dropDir.appendingPathComponent("shadow.json"))
    try importer.remove(info)
    try importer.remove(importing.catalog.info(id: "kjv")!)
    importing.catalog.rescan()
    check(importing.catalog.available.map(\.id) == ["kjv", "dby", "dropped"], "removed import disappears, bundled stay: \(importing.catalog.available.map(\.id))")
    check(!FileManager.default.fileExists(atPath: info.url.path), "file deleted")
    p.translation = "test-1"
    let fallback = try importing.content(for: date(2026, 11, 3, 15, 16), preferences: p, calendar: utc, locale: us)
    check(fallback.translation == "KJV", "a removed translation falls back to KJV")
    check(!VerseClock.defaultImportDirectories().isEmpty && VerseClock.defaultImportDirectories().allSatisfy { $0.path.hasSuffix("BibleTime/Translations") }, "default import directories: \(VerseClock.defaultImportDirectories().map(\.path))")
} catch {
    check(false, "import section threw \(error)")
}

section("seeded generator")
var g1 = SeededGenerator(time: ClockTime(hour: 3, minute: 16, dayOrdinal: 7), salt: "kjv")
var g2 = SeededGenerator(time: ClockTime(hour: 3, minute: 16, dayOrdinal: 7), salt: "kjv")
var g3 = SeededGenerator(time: ClockTime(hour: 3, minute: 16, dayOrdinal: 8), salt: "kjv")
check(g1.next() == g2.next(), "same seed, same sequence")
check(g1.next() != g3.next(), "different day, different sequence")

print("\n\(passes) passed, \(failures) failed")
exit(failures == 0 ? 0 : 1)
