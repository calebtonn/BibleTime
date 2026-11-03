import Foundation

/// Build-time quality flags attached to each verse by `Tools/build_verses.py`.
public struct VerseFlags: OptionSet, Codable, Sendable, Hashable {
    public let rawValue: Int
    public init(rawValue: Int) { self.rawValue = rawValue }

    /// Reads badly on its own (genealogies, name lists, mid-sentence fragments). Used only as a last resort.
    public static let exclude = VerseFlags(rawValue: 1)
    /// Usable but weaker standalone (trailing comma, dialogue tag, very short). Picked only when nothing better exists.
    public static let penalize = VerseFlags(rawValue: 2)
}

/// One verse of one translation.
public struct Verse: Equatable, Sendable {
    public let ref: VerseRef
    public let text: String
    public let flags: VerseFlags

    public init(ref: VerseRef, text: String, flags: VerseFlags = []) {
        self.ref = ref
        self.text = text
        self.flags = flags
    }

    /// True when the text ends mid-sentence (comma or dash), the only reliable fragment signal in KJV punctuation.
    public var endsMidSentence: Bool {
        guard let last = text.unicodeScalars.last else { return false }
        return last == "," || last == "\u{2014}" || last == "-" || last == "\u{2013}"
    }
}

/// Provenance of a bundled translation, copied from the generated JSON.
public struct TranslationSource: Codable, Equatable, Sendable {
    public let publisher: String
    public let edition: String
    public let url: String
    public let sourceFilesDated: String?
    public let generated: String?
    public let sha256: String?
    public let license: String
}

/// A translation loaded from `Resources/Translations/<id>.json`, indexed for O(1) chapter:verse lookup.
public final class Translation: @unchecked Sendable {
    public let id: String
    public let name: String
    public let abbreviation: String
    public let language: String
    public let source: TranslationSource?
    public let verseCount: Int

    private let byRef: [VerseRef: Verse]
    /// Key: chapter * 100 + verse. Value: verses in canonical book order.
    private let bySlot: [Int: [Verse]]

    private struct File: Decodable {
        let id: String
        let name: String
        let abbreviation: String
        let language: String?
        let source: TranslationSource?
        let verses: [Row]

        struct Row: Decodable {
            let book: String
            let chapter: Int
            let verse: Int
            let text: String
            let flags: Int

            init(from decoder: Decoder) throws {
                var c = try decoder.unkeyedContainer()
                book = try c.decode(String.self)
                chapter = try c.decode(Int.self)
                verse = try c.decode(Int.self)
                text = try c.decode(String.self)
                flags = c.isAtEnd ? 0 : try c.decode(Int.self)
            }
        }
    }

    public init(data: Data, canon: Canon) throws {
        let file = try JSONDecoder().decode(File.self, from: data)
        id = file.id
        name = file.name
        abbreviation = file.abbreviation
        language = file.language ?? "en"
        source = file.source

        var byRef: [VerseRef: Verse] = [:]
        var bySlot: [Int: [Verse]] = [:]
        byRef.reserveCapacity(file.verses.count)
        for row in file.verses {
            let ref = VerseRef(book: row.book, chapter: row.chapter, verse: row.verse)
            let verse = Verse(ref: ref, text: row.text, flags: VerseFlags(rawValue: row.flags))
            byRef[ref] = verse
            if row.chapter < 100 && row.verse < 100 {
                bySlot[row.chapter * 100 + row.verse, default: []].append(verse)
            }
        }
        for key in bySlot.keys {
            bySlot[key]?.sort { a, b in
                (canon.order(of: a.ref.book) ?? .max) < (canon.order(of: b.ref.book) ?? .max)
            }
        }
        self.byRef = byRef
        self.bySlot = bySlot
        self.verseCount = byRef.count
    }

    public subscript(ref: VerseRef) -> Verse? { byRef[ref] }

    /// Every verse numbered `chapter:verse` across all books, in canonical order.
    public func verses(chapter: Int, verse: Int) -> [Verse] {
        guard chapter > 0, chapter < 100, verse > 0, verse < 100 else { return [] }
        return bySlot[chapter * 100 + verse] ?? []
    }
}

/// Describes a translation that can be loaded, without loading it.
public struct TranslationInfo: Equatable, Sendable, Identifiable {
    public let id: String
    public let name: String
    public let abbreviation: String
    public let url: URL

    public init(id: String, name: String, abbreviation: String, url: URL) {
        self.id = id
        self.name = name
        self.abbreviation = abbreviation
        self.url = url
    }
}

/// Finds translation files in a resources directory and loads them on demand (cached).
public final class TranslationCatalog: @unchecked Sendable {
    public let canon: Canon
    public let available: [TranslationInfo]
    private var loaded: [String: Translation] = [:]
    private let lock = NSLock()

    /// Known translation metadata so the picker can list names without parsing the full files.
    private static let known: [(id: String, name: String, abbreviation: String)] = [
        ("kjv", "King James Version", "KJV"),
        ("web", "World English Bible", "WEB"),
        ("asv", "American Standard Version", "ASV"),
    ]

    public init(canon: Canon, translationsDirectory: URL) {
        self.canon = canon
        var found: [TranslationInfo] = []
        for entry in TranslationCatalog.known {
            let url = translationsDirectory.appendingPathComponent("\(entry.id).json")
            if FileManager.default.fileExists(atPath: url.path) {
                found.append(TranslationInfo(id: entry.id, name: entry.name, abbreviation: entry.abbreviation, url: url))
            }
        }
        self.available = found
    }

    public func info(id: String) -> TranslationInfo? { available.first { $0.id == id } }

    /// Load (or return the cached) translation. Falls back to the first available translation when `id` is unknown.
    public func translation(id: String) throws -> Translation? {
        lock.lock(); defer { lock.unlock() }
        if let cached = loaded[id] { return cached }
        guard let info = info(id: id) ?? available.first else { return nil }
        if let cached = loaded[info.id] { return cached }
        let data = try Data(contentsOf: info.url)
        let translation = try Translation(data: data, canon: canon)
        loaded[info.id] = translation
        return translation
    }
}
