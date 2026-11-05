// Translations: loading, the catalog of bundled and imported files, and the importer.
// Mirrors Sources/BibleTimeCore/Translation.swift.
#include <algorithm>
#include <climits>
#include <cstdio>
#include <system_error>

#include "Core.h"
#include "JsonReader.h"
#include "Unicode.h"

namespace bt {

namespace fs = std::filesystem;

// MARK: - Unicode helpers

using unicode::decodeScalar;

size_t characterCount(std::string_view utf8) {
    size_t count = 0;
    size_t i = 0;
    unicode::GraphemeBreaker breaker;
    while (i < utf8.size()) {
        if (breaker.startsCluster(decodeScalar(utf8, i))) ++count;
    }
    return count;
}

bool Verse::endsMidSentence() const {
    if (text.empty()) return false;
    // Find the start of the last scalar.
    size_t start = text.size() - 1;
    while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) --start;
    size_t i = start;
    const uint32_t last = decodeScalar(text, i);
    return last == ',' || last == 0x2014 || last == '-' || last == 0x2013;
}

// MARK: - Translation

namespace {

/// A decoding failure, phrased like Foundation's DecodingError.localizedDescription so import messages match the
/// Mac ("The data couldn’t be read because it is missing."), with the technical detail kept in `detail`.
struct DecodeError : std::runtime_error {
    DecodeError(bool isMissing, const std::string& detail) : std::runtime_error(detail), missing(isMissing) {}
    bool missing;
};

[[noreturn]] void missingKey(const char* key) { throw DecodeError(true, std::string("missing \"") + key + "\""); }
[[noreturn]] void wrongType(const std::string& what) { throw DecodeError(false, what + " has the wrong type"); }

const std::string& requireString(const json::Value& v, const char* key) {
    if (v.isNull()) missingKey(key);
    if (!v.isString()) wrongType(std::string("\"") + key + "\"");
    return v.asString();
}

int requireInt(const json::Value& v, const char* what) {
    if (!v.isInteger() || v.asInt() < INT_MIN || v.asInt() > INT_MAX) wrongType(what);
    return static_cast<int>(v.asInt());
}

/// TranslationSource: publisher, edition, url and license are required strings; the rest optional strings.
std::string decodeSource(const json::Value& source) {
    if (!source.isObject()) wrongType("\"source\"");
    for (const char* key : {"publisher", "edition", "url", "license"}) {
        const json::Value* v = source.find(key);
        if (!v) missingKey(key);
        requireString(*v, key);
    }
    for (const char* key : {"sourceFilesDated", "generated", "sha256"}) {
        const json::Value* v = source.find(key);
        if (v && !v->isNull() && !v->isString()) wrongType(std::string("\"") + key + "\"");
    }
    return source.find("license")->asString();
}

}  // namespace

std::shared_ptr<Translation> Translation::fromJSON(std::string_view data, const Canon& canon, fs::path sourcePath) {
    std::shared_ptr<Translation> t(new Translation());
    t->sourcePath_ = std::move(sourcePath);

    json::detail::Reader reader(data);
    std::optional<std::string> idValue, nameValue, abbreviationValue;
    bool haveVerses = false;
    std::vector<Verse> storage;

    if (reader.peek() != '{') wrongType("the top-level value");
    reader.expect('{');
    if (!reader.consume('}')) {
        std::string key, scratch;
        do {
            reader.parseString(key);
            reader.expect(':');
            if (key == "verses") {
                // Stream the rows: [[book, chapter, verse, text, flags?, ...], ...]
                storage.clear();
                storage.reserve(32000);
                if (reader.peek() == 'n') missingKey("verses");
                if (reader.peek() != '[') wrongType("\"verses\"");
                reader.expect('[');
                if (!reader.consume(']')) {
                    do {
                        if (reader.peek() != '[') wrongType("a verse row");
                        reader.expect('[');
                        // Too few elements is Swift's valueNotFound ("missing"); anything else malformed is a type error.
                        const auto separator = [&reader](const char* what) {
                            if (reader.peek() == ']') missingKey(what);
                            reader.expect(',');
                        };
                        Verse verse;
                        if (reader.peek() == ']') missingKey("book");
                        if (reader.peek() != '"') wrongType("a verse row's book");
                        reader.parseString(verse.ref.book);
                        separator("chapter");
                        if (reader.peek() == '"' || reader.peek() == 'n') wrongType("a verse row's chapter");
                        verse.ref.chapter = requireInt(reader.parseNumber(), "a verse row's chapter");
                        separator("verse");
                        if (reader.peek() == '"' || reader.peek() == 'n') wrongType("a verse row's verse");
                        verse.ref.verse = requireInt(reader.parseNumber(), "a verse row's verse");
                        separator("text");
                        if (reader.peek() != '"') wrongType("a verse row's text");
                        reader.parseString(scratch);
                        verse.text.assign(scratch);
                        if (reader.consume(',')) {
                            const char c = reader.peek();
                            if (c != '-' && (c < '0' || c > '9')) wrongType("a verse row's flags");
                            verse.flags = requireInt(reader.parseNumber(), "a verse row's flags");
                            // Swift's unkeyed decoding ignores any further elements.
                            while (reader.consume(',')) reader.parseValue(1);
                        }
                        reader.expect(']');
                        storage.push_back(std::move(verse));
                    } while (reader.consume(','));
                    reader.expect(']');
                }
                haveVerses = true;
            } else if (key == "id" || key == "name" || key == "abbreviation") {
                const json::Value v = reader.parseValue(1);
                std::string s = requireString(v, key.c_str());
                (key == "id" ? idValue : key == "name" ? nameValue : abbreviationValue) = std::move(s);
            } else if (key == "language") {
                const json::Value v = reader.parseValue(1);
                if (!v.isNull() && !v.isString()) wrongType("\"language\"");
                t->language_ = v.isString() ? v.asString() : "en";
            } else if (key == "source") {
                const json::Value v = reader.parseValue(1);
                t->license_ = v.isNull() ? std::string() : decodeSource(v);
            } else {
                reader.parseValue(1);  // unknown keys are ignored, as by JSONDecoder
            }
        } while (reader.consume(','));
        reader.expect('}');
    }
    reader.finish();

    if (!idValue) missingKey("id");
    if (!nameValue) missingKey("name");
    if (!abbreviationValue) missingKey("abbreviation");
    if (!haveVerses) missingKey("verses");

    t->id_ = TranslationHeader::normalizeID(*idValue);
    t->name_ = std::move(*nameValue);
    t->abbreviation_ = std::move(*abbreviationValue);
    t->storage_ = std::move(storage);

    // Index. byRef: later duplicates replace earlier ones (Swift dictionary assignment); bySlot keeps every row.
    t->byRef_.reserve(t->storage_.size());
    t->bySlot_.resize(10000);
    // Canonical order per row, looked up once per run of rows from the same book (unknown books sort last).
    std::vector<int> order(t->storage_.size());
    const std::string* lastBook = nullptr;
    int lastOrder = INT_MAX;
    for (size_t i = 0; i < t->storage_.size(); ++i) {
        const Verse& v = t->storage_[i];
        if (!lastBook || *lastBook != v.ref.book) {
            t->bookCodes_.insert(v.ref.book);
            lastBook = &v.ref.book;
            const int o = canon.order(v.ref.book);
            lastOrder = o < 0 ? INT_MAX : o;
        }
        order[i] = lastOrder;
        t->byRef_[v.ref] = i;
        if (v.ref.chapter < 100 && v.ref.verse < 100) {
            // Same key as Swift (chapter * 100 + verse); keys outside 0..<10000 can never be looked up.
            const int64_t key = int64_t{v.ref.chapter} * 100 + v.ref.verse;
            if (key >= 0 && key < 10000) t->bySlot_[static_cast<size_t>(key)].push_back(&v);
        }
    }
    const Verse* base = t->storage_.data();
    const auto less = [&](const Verse* a, const Verse* b) { return order[size_t(a - base)] < order[size_t(b - base)]; };
    for (auto& slot : t->bySlot_) {
        // Stable, so rows of the same book keep file order (Swift's sort is stable too).
        if (slot.size() > 1) std::stable_sort(slot.begin(), slot.end(), less);
    }
    return t;
}

const Verse* Translation::find(const VerseRef& ref) const {
    const auto it = byRef_.find(ref);
    return it == byRef_.end() ? nullptr : &storage_[it->second];
}

const std::vector<const Verse*>& Translation::verses(int chapter, int verse) const {
    static const std::vector<const Verse*> none;
    if (chapter <= 0 || chapter >= 100 || verse <= 0 || verse >= 100) return none;
    return bySlot_[static_cast<size_t>(chapter * 100 + verse)];
}

// MARK: - TranslationHeader

std::string TranslationHeader::normalizeID(std::string_view id) {
    return unicode::lowercased(unicode::trimWhitespace(id));
}

namespace {

bool isValidUTF8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        size_t n;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) n = 2;
        else if (c >= 0xE0 && c <= 0xEF) {
            n = 3;
            if (c == 0xE0) lo = 0xA0;
            if (c == 0xED) hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            n = 4;
            if (c == 0xF0) lo = 0x90;
            if (c == 0xF4) hi = 0x8F;
        } else return false;
        if (i + n > s.size()) return false;
        const auto c1 = static_cast<unsigned char>(s[i + 1]);
        if (c1 < lo || c1 > hi) return false;
        for (size_t k = 2; k < n; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += n;
    }
    return true;
}

std::optional<TranslationHeader> decodeHeader(std::string_view json) {
    try {
        const json::Value v = json::parse(json);
        const json::Value* id = v.find("id");
        const json::Value* name = v.find("name");
        const json::Value* abbreviation = v.find("abbreviation");
        if (!id || !id->isString() || !name || !name->isString() || !abbreviation || !abbreviation->isString()) {
            return std::nullopt;
        }
        return TranslationHeader{TranslationHeader::normalizeID(id->asString()), name->asString(), abbreviation->asString()};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

TranslationHeader TranslationHeader::fromFile(const fs::path& path) {
    // Files written by build_verses.py put the header before the multi-megabyte "verses" array, so the fast path
    // reads only the first 4 KB, cuts at that key and closes the object. Anything else parses the whole file.
    {
#ifdef _WIN32
        FILE* f = _wfopen(path.c_str(), L"rb");
#else
        FILE* f = std::fopen(path.c_str(), "rb");
#endif
        if (f) {
            char buffer[4096];
            const size_t n = std::fread(buffer, 1, sizeof buffer, f);
            std::fclose(f);
            std::string_view head(buffer, n);
            // Like String(data:encoding:.utf8) on the 4096 bytes, else on the first 4000.
            bool decoded = isValidUTF8(head);
            if (!decoded && head.size() > 4000) {
                head = head.substr(0, 4000);
                decoded = isValidUTF8(head);
            }
            if (decoded) {
                const size_t at = head.find("\"verses\"");
                if (at != std::string_view::npos) {
                    std::string prefix = text::trimASCII(head.substr(0, at), true);
                    if (!prefix.empty() && prefix.back() == ',') prefix.pop_back();
                    prefix += '}';
                    if (auto header = decodeHeader(prefix)) return *header;
                }
            }
        }
    }
    const auto data = text::readFile(path);
    if (data) {
        if (auto header = decodeHeader(*data)) return *header;
    }
    throw ImportError::invalidFormat("missing id, name or abbreviation");
}

// MARK: - Resource loader

ResourceLoader directoryLoader(fs::path directory) {
    return [directory = std::move(directory)](const std::string& name) -> std::optional<std::string> {
        return text::readFile(directory / fs::u8path(name));
    };
}

// MARK: - TranslationCatalog

const std::vector<TranslationCatalog::BundledEntry>& TranslationCatalog::bundled() {
    static const std::vector<BundledEntry> entries = {
        {"kjv", "King James Version", "KJV"},
        {"dby", "Darby Translation", "DBY"},
    };
    return entries;
}

bool TranslationCatalog::isBundledID(std::string_view id) {
    for (const auto& entry : bundled()) {
        if (id == entry.id) return true;
    }
    return false;
}

TranslationCatalog::TranslationCatalog(const Canon& canon, ResourceLoader loader, std::vector<fs::path> importDirectories)
    : canon_(canon), loader_(std::move(loader)), importDirectories_(std::move(importDirectories)) {
    // Bundled resources never change while the process runs, so probe them once rather than on every rescan.
    for (const auto& entry : bundled()) {
        if (loader_ && loader_(std::string("Translations/") + entry.id + ".json")) {
            TranslationInfo info;
            info.id = entry.id;
            info.name = entry.name;
            info.abbreviation = entry.abbreviation;
            bundledFound_.push_back(std::move(info));
        }
    }
    rescan();
}

std::vector<TranslationInfo> TranslationCatalog::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return found_;
}

std::optional<TranslationInfo> TranslationCatalog::info(std::string_view id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : found_) {
        if (entry.id == id) return entry;
    }
    return std::nullopt;
}

namespace {

bool hasSuffix(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

namespace {

NameComparator& nameComparator() {
    static NameComparator comparator;
    return comparator;
}

}  // namespace

void setNameComparator(NameComparator comparator) { nameComparator() = std::move(comparator); }

int compareNames(const std::string& a, const std::string& b) {
    const NameComparator& comparator = nameComparator();
    return comparator ? comparator(a, b) : unicode::caseInsensitiveCompare(a, b);
}

void TranslationCatalog::rescan() {
    std::vector<TranslationInfo> list = bundledFound_;
    std::vector<TranslationInfo> imported;
    std::unordered_set<std::string> seen;
    for (const auto& entry : list) seen.insert(entry.id);
    for (const fs::path& directory : importDirectories_) {
        std::vector<std::string> names;
        std::error_code ec;
        fs::directory_iterator it(directory, ec), end;
        if (ec) continue;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            names.push_back(it->path().filename().u8string());
        }
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            if (!hasSuffix(name, ".json")) continue;
            const fs::path path = directory / fs::u8path(name);
            try {
                const TranslationHeader header = TranslationHeader::fromFile(path);
                if (seen.count(header.id)) continue;
                seen.insert(header.id);
                TranslationInfo entry;
                entry.id = header.id;
                entry.name = header.name;
                entry.abbreviation = header.abbreviation;
                entry.path = path;
                entry.isImported = true;
                imported.push_back(std::move(entry));
            } catch (const std::exception&) {
                // Not a translation file; skip it like the Swift `try?`.
            }
        }
    }
    std::stable_sort(imported.begin(), imported.end(),
                     [](const TranslationInfo& a, const TranslationInfo& b) { return compareNames(a.name, b.name) < 0; });
    list.insert(list.end(), imported.begin(), imported.end());

    std::lock_guard<std::mutex> lock(mutex_);
    found_ = std::move(list);
    // Imported files can be replaced in place, so drop their cached copies; bundled ones never change.
    for (auto it = loaded_.begin(); it != loaded_.end();) {
        if (isBundledID(it->first)) ++it;
        else it = loaded_.erase(it);
    }
}

std::shared_ptr<const Translation> TranslationCatalog::translation(std::string_view id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key(id);
    if (const auto it = loaded_.find(key); it != loaded_.end()) return it->second;
    const TranslationInfo* chosen = nullptr;
    for (const auto& candidate : found_) {
        if (candidate.id == key) {
            chosen = &candidate;
            break;
        }
    }
    if (!chosen && !found_.empty()) chosen = &found_.front();
    if (!chosen) return nullptr;
    if (const auto it = loaded_.find(chosen->id); it != loaded_.end()) return it->second;

    std::optional<std::string> data;
    if (chosen->isImported) {
        data = text::readFile(chosen->path);
        if (!data) throw std::runtime_error("Could not read " + chosen->path.u8string());
    } else {
        const std::string name = "Translations/" + chosen->id + ".json";
        data = loader_ ? loader_(name) : std::nullopt;
        if (!data) throw std::runtime_error("BibleTime resource missing: " + name);
    }
    std::shared_ptr<const Translation> translation = Translation::fromJSON(*data, canon_, chosen->path);
    loaded_[chosen->id] = translation;
    return translation;
}

// MARK: - Import

ImportError ImportError::invalidFormat(const std::string& detail) {
    return ImportError(Kind::InvalidFormat, "This is not a BibleTime translation file (" + detail +
                                                "). Convert your copy with Tools/build_verses.py first.");
}

ImportError ImportError::bundledID(const std::string& id) {
    return ImportError(Kind::BundledID,
                       "The id \"" + id + "\" belongs to a bundled translation; give the file a different id.");
}

ImportError ImportError::tooFewVerses(size_t n) {
    return ImportError(Kind::TooFewVerses,
                       "The file holds only " + std::to_string(n) + " verses; a translation needs the whole Bible.");
}

ImportError ImportError::unknownBooks(std::vector<std::string> sortedCodes) {
    std::string list;
    for (size_t i = 0; i < sortedCodes.size() && i < 5; ++i) {
        if (i) list += ", ";
        list += sortedCodes[i];
    }
    return ImportError(Kind::UnknownBooks, "Unknown book codes: " + list + ". Books must use USFM codes such as JHN.",
                       std::move(sortedCodes));
}

bool TranslationImporter::isValidID(std::string_view id) {
    // Swift: !id.isEmpty && id.allSatisfy { $0.isLetter || $0.isNumber || $0 == "-" || $0 == "_" }, one Character
    // (extended grapheme cluster) at a time; isLetter/isNumber look at the cluster's first scalar.
    if (id.empty()) return false;
    unicode::GraphemeBreaker breaker;
    size_t i = 0;
    uint32_t first = 0;
    size_t length = 0;  // scalars in the current cluster
    const auto clusterOK = [&] { return unicode::isLetterOrNumber(first) || (length == 1 && (first == '-' || first == '_')); };
    while (i < id.size()) {
        const uint32_t u = decodeScalar(id, i);
        if (breaker.startsCluster(u)) {
            if (length && !clusterOK()) return false;
            first = u;
            length = 0;
        }
        ++length;
    }
    return clusterOK();
}

std::pair<TranslationHeader, size_t> TranslationImporter::validate(std::string_view data) const {
    std::shared_ptr<Translation> translation;
    try {
        translation = Translation::fromJSON(data, canon_);
    } catch (const DecodeError& e) {
        // Foundation's wording for DecodingError.keyNotFound / .valueNotFound and .typeMismatch.
        throw ImportError::invalidFormat(e.missing ? "The data couldn’t be read because it is missing."
                                                   : "The data couldn’t be read because it isn’t in the correct format.");
    } catch (const std::exception&) {
        throw ImportError::invalidFormat("The data couldn’t be read because it isn’t in the correct format.");
    }
    const std::string& id = translation->id();
    if (!isValidID(id)) throw ImportError::invalidFormat("id must be letters, digits, - or _");
    if (translation->name().empty() || translation->abbreviation().empty()) {
        throw ImportError::invalidFormat("name and abbreviation are required");
    }
    if (TranslationCatalog::isBundledID(id)) throw ImportError::bundledID(id);
    if (translation->verseCount() < 5000) throw ImportError::tooFewVerses(translation->verseCount());
    std::vector<std::string> unknown;
    for (const std::string& code : translation->bookCodes()) {
        if (!canon_.book(code)) unknown.push_back(code);
    }
    if (!unknown.empty()) {
        std::sort(unknown.begin(), unknown.end());
        throw ImportError::unknownBooks(std::move(unknown));
    }
    return {TranslationHeader{id, translation->name(), translation->abbreviation()}, translation->verseCount()};
}

namespace {

void writeFile(const fs::path& path, std::string_view data) {
#ifdef _WIN32
    FILE* f = _wfopen(path.c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) throw std::runtime_error("Could not write " + path.u8string());
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed) {
        std::error_code ec;
        fs::remove(path, ec);
        throw std::runtime_error("Could not write " + path.u8string());
    }
}

}  // namespace

TranslationInfo TranslationImporter::importFile(const fs::path& source) const {
    const auto data = text::readFile(source);
    if (!data) throw std::runtime_error("Could not read " + source.u8string());
    const TranslationHeader header = validate(*data).first;
    fs::create_directories(destination_);
    const fs::path target = destination_ / fs::u8path(header.id + ".json");
    const fs::path temporary = destination_ / fs::u8path("." + header.id + ".json.part");
    writeFile(temporary, *data);
    // Remove first: rename() over an existing file fails on Windows with some standard libraries.
    if (fs::exists(target)) fs::remove(target);
    fs::rename(temporary, target);
    TranslationInfo info;
    info.id = header.id;
    info.name = header.name;
    info.abbreviation = header.abbreviation;
    info.path = target;
    info.isImported = true;
    return info;
}

void TranslationImporter::remove(const TranslationInfo& info) const {
    if (!info.isImported || TranslationCatalog::isBundledID(info.id)) return;
    // Like FileManager.removeItem, a missing file is an error.
    if (!fs::remove(info.path)) {
        throw std::runtime_error("Could not remove " + info.path.u8string() + ": no such file");
    }
}

}  // namespace bt
