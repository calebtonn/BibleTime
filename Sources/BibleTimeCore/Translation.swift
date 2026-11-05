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
    /// Where the file was loaded from (nil for in-memory data).
    public let sourceURL: URL?
    /// Every book code present in the file.
    public let bookCodes: Set<String>

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

    public init(data: Data, canon: Canon, sourceURL: URL? = nil) throws {
        let file = try JSONDecoder().decode(File.self, from: data)
        self.sourceURL = sourceURL
        id = TranslationHeader.normalize(id: file.id)
        name = file.name
        abbreviation = file.abbreviation
        language = file.language ?? "en"
        source = file.source

        var byRef: [VerseRef: Verse] = [:]
        var bySlot: [Int: [Verse]] = [:]
        var books = Set<String>()
        byRef.reserveCapacity(file.verses.count)
        for row in file.verses {
            books.insert(row.book)
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
        self.bookCodes = books
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
    /// True for translations the user imported (they live outside the bundle and can be removed).
    public let isImported: Bool

    public init(id: String, name: String, abbreviation: String, url: URL, isImported: Bool = false) {
        self.id = id
        self.name = name
        self.abbreviation = abbreviation
        self.url = url
        self.isImported = isImported
    }

    /// "Darby Translation (DBY)"
    public var displayName: String { "\(name) (\(abbreviation))" }
}

/// Finds translation files (bundled and imported) and loads them on demand (cached).
///
/// Bundled files live in the bundle's `Resources/Translations`. Imported files live in the first directory of
/// `importDirectories` (where `TranslationImporter` writes) and are also looked for in the others, so a user can
/// drop a converted file in place by hand.
public final class TranslationCatalog: @unchecked Sendable {
    public let canon: Canon
    public let bundledDirectory: URL
    public let importDirectories: [URL]
    private var found: [TranslationInfo] = []
    private var loaded: [String: Translation] = [:]
    private let lock = NSLock()

    /// Known bundled translations, so the picker can list names without parsing the full files.
    public static let bundled: [(id: String, name: String, abbreviation: String)] = [
        ("kjv", "King James Version", "KJV"),
        ("dby", "Darby Translation", "DBY"),
    ]

    public static func isBundledID(_ id: String) -> Bool { bundled.contains { $0.id == id } }

    public init(canon: Canon, translationsDirectory: URL, importDirectories: [URL] = []) {
        self.canon = canon
        self.bundledDirectory = translationsDirectory
        self.importDirectories = importDirectories
        rescan()
    }

    /// Bundled translations first, then imported ones sorted by name.
    public var available: [TranslationInfo] {
        lock.lock(); defer { lock.unlock() }
        return found
    }

    /// Re-read both directories (after an import or removal).
    public func rescan() {
        var list: [TranslationInfo] = []
        for entry in TranslationCatalog.bundled {
            let url = bundledDirectory.appendingPathComponent("\(entry.id).json")
            if FileManager.default.fileExists(atPath: url.path) {
                list.append(TranslationInfo(id: entry.id, name: entry.name, abbreviation: entry.abbreviation, url: url))
            }
        }
        var imported: [TranslationInfo] = []
        var seen = Set(list.map(\.id))
        for directory in importDirectories {
            guard let names = try? FileManager.default.contentsOfDirectory(atPath: directory.path) else { continue }
            for name in names.sorted() where name.hasSuffix(".json") {
                let url = directory.appendingPathComponent(name)
                guard let header = try? TranslationHeader(url: url), !seen.contains(header.id) else { continue }
                seen.insert(header.id)
                imported.append(TranslationInfo(id: header.id, name: header.name, abbreviation: header.abbreviation, url: url, isImported: true))
            }
        }
        imported.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        lock.lock()
        found = list + imported
        // Imported files can be replaced in place, so drop their cached copies; bundled ones never change.
        loaded = loaded.filter { TranslationCatalog.isBundledID($0.key) }
        lock.unlock()
    }

    public func info(id: String) -> TranslationInfo? { available.first { $0.id == id } }

    /// Load (or return the cached) translation. Falls back to the first available translation when `id` is unknown
    /// (for example after an imported translation was removed).
    public func translation(id: String) throws -> Translation? {
        lock.lock(); defer { lock.unlock() }
        if let cached = loaded[id] { return cached }
        guard let info = found.first(where: { $0.id == id }) ?? found.first else { return nil }
        if let cached = loaded[info.id] { return cached }
        let data = try Data(contentsOf: info.url)
        let translation = try Translation(data: data, canon: canon, sourceURL: info.url)
        loaded[info.id] = translation
        return translation
    }
}

/// The identifying fields of a translation file, decoded without touching the verse array.
public struct TranslationHeader: Decodable, Equatable, Sendable {
    public let id: String
    public let name: String
    public let abbreviation: String

    public init(id: String, name: String, abbreviation: String) {
        self.id = TranslationHeader.normalize(id: id)
        self.name = name
        self.abbreviation = abbreviation
    }

    private enum CodingKeys: String, CodingKey { case id, name, abbreviation }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = TranslationHeader.normalize(id: try c.decode(String.self, forKey: .id))
        name = try c.decode(String.self, forKey: .name)
        abbreviation = try c.decode(String.self, forKey: .abbreviation)
    }

    /// Ids are compared case-insensitively and used as file names, so they are stored lowercased and trimmed.
    public static func normalize(id: String) -> String { id.trimmingCharacters(in: .whitespaces).lowercased() }

    public init(url: URL) throws {
        // Files written by build_verses.py put the header before the multi-megabyte "verses" array, so the fast path
        // reads only the first 4 KB, cuts at that key and closes the object. Any other key order falls back to
        // decoding the whole file (JSONDecoder ignores the verses because the struct has no such key).
        if let head = try? FileHandle(forReadingFrom: url).readData(ofLength: 4096),
           let text = String(data: head, encoding: .utf8) ?? String(data: head.prefix(4000), encoding: .utf8),
           let range = text.range(of: "\"verses\"") {
            var prefix = String(text[..<range.lowerBound]).trimmingCharacters(in: .whitespacesAndNewlines)
            if prefix.hasSuffix(",") { prefix.removeLast() }
            prefix += "}"
            if let header = try? JSONDecoder().decode(TranslationHeader.self, from: Data(prefix.utf8)) {
                self = header
                return
            }
        }
        do {
            self = try JSONDecoder().decode(TranslationHeader.self, from: try Data(contentsOf: url))
        } catch {
            throw TranslationImporter.ImportError.invalidFormat("missing id, name or abbreviation")
        }
    }
}

/// Validates a translation file produced by `Tools/build_verses.py` and copies it into the import directory.
public struct TranslationImporter: Sendable {
    public let canon: Canon
    public let destination: URL

    public enum ImportError: Error, CustomStringConvertible, Equatable {
        case invalidFormat(String)
        case bundledID(String)
        case tooFewVerses(Int)
        case unknownBooks([String])

        public var description: String {
            switch self {
            case .invalidFormat(let detail): return "This is not a BibleTime translation file (\(detail)). Convert your copy with Tools/build_verses.py first."
            case .bundledID(let id): return "The id \"\(id)\" belongs to a bundled translation; give the file a different id."
            case .tooFewVerses(let n): return "The file holds only \(n) verses; a translation needs the whole Bible."
            case .unknownBooks(let codes): return "Unknown book codes: \(codes.prefix(5).joined(separator: ", ")). Books must use USFM codes such as JHN."
            }
        }
    }

    public init(canon: Canon, destination: URL) {
        self.canon = canon
        self.destination = destination
    }

    /// Parse and sanity-check a file without copying it.
    public func validate(data: Data) throws -> (header: TranslationHeader, verseCount: Int) {
        let translation: Translation
        do {
            translation = try Translation(data: data, canon: canon)
        } catch {
            throw ImportError.invalidFormat("\(error.localizedDescription)")
        }
        let id = translation.id
        guard !id.isEmpty, id.allSatisfy({ $0.isLetter || $0.isNumber || $0 == "-" || $0 == "_" }) else {
            throw ImportError.invalidFormat("id must be letters, digits, - or _")
        }
        guard !translation.name.isEmpty, !translation.abbreviation.isEmpty else { throw ImportError.invalidFormat("name and abbreviation are required") }
        if TranslationCatalog.isBundledID(id) { throw ImportError.bundledID(id) }
        guard translation.verseCount >= 5000 else { throw ImportError.tooFewVerses(translation.verseCount) }
        let unknown = translation.bookCodes.filter { canon.book(code: $0) == nil }
        if !unknown.isEmpty { throw ImportError.unknownBooks(unknown.sorted()) }
        return (TranslationHeader(id: id, name: translation.name, abbreviation: translation.abbreviation), translation.verseCount)
    }

    /// Validate `source` and copy it to `destination/<id>.json`, replacing any previous import with that id.
    @discardableResult
    public func importFile(at source: URL) throws -> TranslationInfo {
        let data = try Data(contentsOf: source)
        let (header, _) = try validate(data: data)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let target = destination.appendingPathComponent("\(header.id).json")
        let temporary = destination.appendingPathComponent(".\(header.id).json.part")
        try data.write(to: temporary, options: .atomic)
        if FileManager.default.fileExists(atPath: target.path) { try FileManager.default.removeItem(at: target) }
        try FileManager.default.moveItem(at: temporary, to: target)
        return TranslationInfo(id: header.id, name: header.name, abbreviation: header.abbreviation, url: target, isImported: true)
    }

    /// Delete an imported translation file. Bundled translations are never touched.
    public func remove(_ info: TranslationInfo) throws {
        guard info.isImported, !TranslationCatalog.isBundledID(info.id) else { return }
        try FileManager.default.removeItem(at: info.url)
    }
}
