import Foundation

/// One book of the Protestant 66-book canon, as shipped in `Resources/books.json`.
public struct Book: Codable, Equatable, Sendable {
    /// USFM/SIL three-letter code, e.g. "JHN".
    public let code: String
    /// Display name, e.g. "John", "Psalm", "1 John".
    public let name: String
    /// Short display name, e.g. "Jn".
    public let short: String
    /// Number of chapters in the book.
    public let chapters: Int

    public init(code: String, name: String, short: String, chapters: Int) {
        self.code = code
        self.name = name
        self.short = short
        self.chapters = chapters
    }
}

/// A chapter-and-verse reference inside a book.
public struct VerseRef: Hashable, Codable, Sendable, CustomStringConvertible {
    public let book: String
    public let chapter: Int
    public let verse: Int

    public init(book: String, chapter: Int, verse: Int) {
        self.book = book
        self.chapter = chapter
        self.verse = verse
    }

    /// The reference one verse later in the same chapter.
    public var next: VerseRef { VerseRef(book: book, chapter: chapter, verse: verse + 1) }

    public var description: String { "\(book) \(chapter):\(verse)" }
}

/// The ordered list of books, with lookups by code.
public struct Canon: Sendable {
    public let books: [Book]
    private let byCode: [String: Int]

    public init(books: [Book]) {
        self.books = books
        var index: [String: Int] = [:]
        for (i, book) in books.enumerated() { index[book.code] = i }
        self.byCode = index
    }

    /// Decode `books.json` (an array of `Book`).
    public init(data: Data) throws {
        self.init(books: try JSONDecoder().decode([Book].self, from: data))
    }

    public func book(code: String) -> Book? {
        guard let i = byCode[code] else { return nil }
        return books[i]
    }

    /// Canonical position of a book (0 = Genesis), or nil when unknown.
    public func order(of code: String) -> Int? { byCode[code] }

    /// Parse a human reference such as "1 John 4:8", "Psalm 23:1" or "Song of Solomon 2:4".
    /// Returns nil when the book name is unknown or the numbers are missing.
    public func parse(reference: String) -> VerseRef? {
        let trimmed = reference.trimmingCharacters(in: .whitespaces)
        guard let colon = trimmed.lastIndex(of: ":") else { return nil }
        let verseText = trimmed[trimmed.index(after: colon)...]
        let beforeColon = trimmed[..<colon]
        guard let space = beforeColon.lastIndex(of: " ") else { return nil }
        let chapterText = beforeColon[beforeColon.index(after: space)...]
        let bookName = beforeColon[..<space].trimmingCharacters(in: .whitespaces)
        guard let chapter = Int(chapterText), let verse = Int(verseText) else { return nil }
        guard let book = book(named: bookName) else { return nil }
        return VerseRef(book: book.code, chapter: chapter, verse: verse)
    }

    /// Find a book by display name, short name, code, or a few common aliases, case-insensitively.
    public func book(named name: String) -> Book? {
        let needle = Canon.normalize(name)
        for book in books {
            if Canon.normalize(book.name) == needle || Canon.normalize(book.short) == needle || book.code.lowercased() == needle {
                return book
            }
        }
        if let alias = Canon.aliases[needle] { return book(code: alias) }
        return nil
    }

    private static func normalize(_ s: String) -> String {
        s.lowercased().replacingOccurrences(of: ".", with: "").split(separator: " ").joined(separator: " ")
    }

    private static let aliases: [String: String] = [
        "psalms": "PSA", "ps": "PSA", "psa": "PSA",
        "song of songs": "SNG", "canticles": "SNG", "song": "SNG",
        "revelation of john": "REV", "revelations": "REV", "apocalypse": "REV",
        "i samuel": "1SA", "ii samuel": "2SA", "i kings": "1KI", "ii kings": "2KI",
        "i chronicles": "1CH", "ii chronicles": "2CH",
        "i corinthians": "1CO", "ii corinthians": "2CO",
        "i thessalonians": "1TH", "ii thessalonians": "2TH",
        "i timothy": "1TI", "ii timothy": "2TI", "i peter": "1PE", "ii peter": "2PE",
        "i john": "1JN", "ii john": "2JN", "iii john": "3JN",
        "1st john": "1JN", "2nd john": "2JN", "3rd john": "3JN",
    ]
}
