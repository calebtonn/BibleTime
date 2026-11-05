// Canon, VerseRef and the small text helpers. Mirrors Sources/BibleTimeCore/Canon.swift.
#include <climits>
#include <cstdio>

#include "Core.h"

namespace bt {

// MARK: - Text utilities

namespace text {

std::string toLowerASCII(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

namespace {
bool isSpaceOrTab(char c) { return c == ' ' || c == '\t'; }
bool isWhitespaceOrNewline(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
}  // namespace

std::string trimASCII(std::string_view s, bool newlines) {
    const auto strip = newlines ? isWhitespaceOrNewline : isSpaceOrTab;
    size_t a = 0, b = s.size();
    while (a < b && strip(s[a])) ++a;
    while (b > a && strip(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

std::string trimASCII(std::string_view s) { return trimASCII(s, false); }

std::optional<std::string> readFile(const std::filesystem::path& path) {
#ifdef _WIN32
    FILE* f = _wfopen(path.c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return std::nullopt;
    std::string data;
    char buffer[65536];
    size_t n;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        const long size = std::ftell(f);
        if (size > 0) data.reserve(static_cast<size_t>(size));
        std::fseek(f, 0, SEEK_SET);
    }
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) data.append(buffer, n);
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) return std::nullopt;
    return data;
}

}  // namespace text

// MARK: - VerseRef

std::string VerseRef::description() const {
    return book + " " + std::to_string(chapter) + ":" + std::to_string(verse);
}

size_t VerseRefHash::operator()(const VerseRef& r) const noexcept {
    size_t h = std::hash<std::string>()(r.book);
    h ^= static_cast<size_t>(static_cast<unsigned>(r.chapter)) * 0x9E3779B1u + 0x7F4A7C15u + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(static_cast<unsigned>(r.verse)) * 0x85EBCA77u + 0xC2B2AE3Du + (h << 6) + (h >> 2);
    return h;
}

// MARK: - Canon

Canon::Canon(std::vector<Book> books) : books_(std::move(books)) {
    // Later duplicates win, like the Swift dictionary built in a loop.
    for (size_t i = 0; i < books_.size(); ++i) byCode_[books_[i].code] = static_cast<int>(i);
}

Canon Canon::fromJSON(std::string_view data) {
    const json::Value root = json::parse(data);
    if (!root.isArray()) throw std::runtime_error("books.json: expected an array of books");
    std::vector<Book> list;
    list.reserve(root.items().size());
    for (const json::Value& item : root.items()) {
        const json::Value* code = item.find("code");
        const json::Value* name = item.find("name");
        const json::Value* shortName = item.find("short");
        const json::Value* chapters = item.find("chapters");
        if (!code || !code->isString() || !name || !name->isString() || !shortName || !shortName->isString() ||
            !chapters || !chapters->isInteger() || chapters->asInt() < INT_MIN || chapters->asInt() > INT_MAX) {
            throw std::runtime_error("books.json: each book needs code, name, short and chapters");
        }
        Book book;
        book.code = code->asString();
        book.name = name->asString();
        book.shortName = shortName->asString();
        book.chapters = static_cast<int>(chapters->asInt());
        list.push_back(std::move(book));
    }
    return Canon(std::move(list));
}

const Book* Canon::book(std::string_view code) const {
    const auto it = byCode_.find(std::string(code));
    return it == byCode_.end() ? nullptr : &books_[static_cast<size_t>(it->second)];
}

int Canon::order(std::string_view code) const {
    const auto it = byCode_.find(std::string(code));
    return it == byCode_.end() ? -1 : it->second;
}

std::optional<int64_t> text::parseInt(std::string_view s) {
    size_t i = 0;
    bool negative = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
        negative = s[0] == '-';
        i = 1;
    }
    if (i == s.size()) return std::nullopt;
    uint64_t value = 0;
    const uint64_t limit = negative ? (uint64_t{1} << 63) : (uint64_t{1} << 63) - 1;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return std::nullopt;
        const auto digit = static_cast<uint64_t>(c - '0');
        if (value > (limit - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return negative ? static_cast<int64_t>(0 - value) : static_cast<int64_t>(value);
}

namespace {

/// Canon.normalize: lowercased, periods removed, runs of spaces collapsed, leading/trailing spaces dropped.
std::string normalizeName(std::string_view s) {
    std::string lowered = text::toLowerASCII(s);
    std::string out;
    out.reserve(lowered.size());
    bool pendingSpace = false;
    for (const char c : lowered) {
        if (c == '.') continue;
        if (c == ' ') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) out += ' ';
        pendingSpace = false;
        out += c;
    }
    return out;
}

const std::unordered_map<std::string, std::string>& aliases() {
    static const std::unordered_map<std::string, std::string> table = {
        {"psalms", "PSA"}, {"ps", "PSA"}, {"psa", "PSA"},
        {"song of songs", "SNG"}, {"canticles", "SNG"}, {"song", "SNG"},
        {"revelation of john", "REV"}, {"revelations", "REV"}, {"apocalypse", "REV"},
        {"i samuel", "1SA"}, {"ii samuel", "2SA"}, {"i kings", "1KI"}, {"ii kings", "2KI"},
        {"i chronicles", "1CH"}, {"ii chronicles", "2CH"},
        {"i corinthians", "1CO"}, {"ii corinthians", "2CO"},
        {"i thessalonians", "1TH"}, {"ii thessalonians", "2TH"},
        {"i timothy", "1TI"}, {"ii timothy", "2TI"}, {"i peter", "1PE"}, {"ii peter", "2PE"},
        {"i john", "1JN"}, {"ii john", "2JN"}, {"iii john", "3JN"},
        {"1st john", "1JN"}, {"2nd john", "2JN"}, {"3rd john", "3JN"},
    };
    return table;
}

}  // namespace

std::optional<VerseRef> Canon::parse(std::string_view reference) const {
    const std::string trimmed = text::trimASCII(reference);
    const size_t colon = trimmed.rfind(':');
    if (colon == std::string::npos) return std::nullopt;
    const std::string_view verseText = std::string_view(trimmed).substr(colon + 1);
    const std::string_view beforeColon = std::string_view(trimmed).substr(0, colon);
    const size_t space = beforeColon.rfind(' ');
    if (space == std::string_view::npos) return std::nullopt;
    const std::string_view chapterText = beforeColon.substr(space + 1);
    const std::string bookName = text::trimASCII(beforeColon.substr(0, space));
    const auto chapter = text::parseInt(chapterText);
    const auto verse = text::parseInt(verseText);
    // Swift's Int is 64-bit; VerseRef uses int, so out-of-range numbers are treated as unparseable.
    if (!chapter || !verse || *chapter < INT_MIN || *chapter > INT_MAX || *verse < INT_MIN || *verse > INT_MAX) {
        return std::nullopt;
    }
    const Book* b = bookNamed(bookName);
    if (!b) return std::nullopt;
    return VerseRef(b->code, static_cast<int>(*chapter), static_cast<int>(*verse));
}

const Book* Canon::bookNamed(std::string_view name) const {
    const std::string needle = normalizeName(name);
    for (const Book& b : books_) {
        if (normalizeName(b.name) == needle || normalizeName(b.shortName) == needle || text::toLowerASCII(b.code) == needle) {
            return &b;
        }
    }
    const auto& table = aliases();
    const auto it = table.find(needle);
    if (it != table.end()) return book(it->second);
    return nullptr;
}

}  // namespace bt
