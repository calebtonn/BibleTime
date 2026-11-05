// Unicode text handling that matches Swift's String; see Unicode.h.
#include "Unicode.h"

#include <algorithm>
#include <iterator>

namespace bt::unicode {

namespace {

struct GraphemeRange {
    uint32_t first, last;
    uint8_t value;
};
struct ScalarRange {
    uint32_t first, last;
};
struct LowerRun {
    uint32_t first, last;
    uint32_t step;
    int32_t delta;
};
struct SpecialLower {
    uint32_t scalar;
    uint32_t lower[3];  // zero-terminated when shorter
};

#include "GraphemeTables.inc"

// Grapheme_Cluster_Break, in the generator's order.
enum Gcb : uint8_t { Other, CR, LF, Control, Extend, ZWJ, RegionalIndicator, Prepend, SpacingMark, L, V, T, LV, LVT };
constexpr uint8_t kGcbMask = 0x0F;
constexpr uint8_t kExtendedPictographic = 0x10;
constexpr uint8_t kConsonant = 0x20;
constexpr uint8_t kLinker = 0x40;
constexpr uint8_t kConjunctExtend = 0x80;

uint8_t graphemeProperties(uint32_t u) {
    if (u >= 0x20 && u < 0x7F) return Other;
    if (u >= 0xAC00 && u <= 0xD7A3) return (u - 0xAC00) % 28 == 0 ? LV : LVT;
    const auto it = std::upper_bound(std::begin(kGraphemeRanges), std::end(kGraphemeRanges), u,
                                     [](uint32_t x, const GraphemeRange& r) { return x < r.first; });
    if (it == std::begin(kGraphemeRanges)) return Other;
    const GraphemeRange& r = *std::prev(it);
    return u <= r.last ? r.value : static_cast<uint8_t>(Other);
}

template <size_t N>
bool inRanges(const ScalarRange (&table)[N], uint32_t u) {
    const auto it = std::upper_bound(std::begin(table), std::end(table), u,
                                     [](uint32_t x, const ScalarRange& r) { return x < r.first; });
    return it != std::begin(table) && u <= std::prev(it)->last;
}

bool isControlLike(uint8_t g) { return g == CR || g == LF || g == Control; }

/// Append the lowercase mapping of one scalar.
void appendLower(std::string& out, uint32_t u) {
    if (u < 0x80) {
        out += static_cast<char>(u >= 'A' && u <= 'Z' ? u + 32 : u);
        return;
    }
    for (const SpecialLower& s : kSpecialLower) {
        if (s.scalar == u) {
            for (const uint32_t x : s.lower) {
                if (x) appendUTF8(out, x);
            }
            return;
        }
    }
    const auto it = std::upper_bound(std::begin(kLowerRuns), std::end(kLowerRuns), u,
                                     [](uint32_t x, const LowerRun& r) { return x < r.first; });
    if (it != std::begin(kLowerRuns)) {
        const LowerRun& r = *std::prev(it);
        if (u <= r.last && (u - r.first) % r.step == 0) {
            appendUTF8(out, static_cast<uint32_t>(static_cast<int64_t>(u) + r.delta));
            return;
        }
    }
    appendUTF8(out, u);
}

}  // namespace

uint32_t decodeScalar(std::string_view s, size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    size_t n = 1;
    uint32_t scalar = c;
    if (c >= 0xF0) {
        n = 4;
        scalar = c & 0x07;
    } else if (c >= 0xE0) {
        n = 3;
        scalar = c & 0x0F;
    } else if (c >= 0xC0) {
        n = 2;
        scalar = c & 0x1F;
    } else if (c >= 0x80) {
        ++i;
        return 0xFFFD;
    }
    if (i + n > s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (size_t k = 1; k < n; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        scalar = (scalar << 6) | (b & 0x3F);
    }
    i += n;
    return scalar;
}

void appendUTF8(std::string& out, uint32_t u) {
    if (u < 0x80) {
        out += static_cast<char>(u);
    } else if (u < 0x800) {
        out += static_cast<char>(0xC0 | (u >> 6));
        out += static_cast<char>(0x80 | (u & 0x3F));
    } else if (u < 0x10000) {
        out += static_cast<char>(0xE0 | (u >> 12));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (u & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (u >> 18));
        out += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (u & 0x3F));
    }
}

bool GraphemeBreaker::startsCluster(uint32_t scalar) {
    const uint8_t p = graphemeProperties(scalar);
    const auto g = static_cast<uint8_t>(p & kGcbMask);
    const auto pg = static_cast<uint8_t>(previous_ & kGcbMask);
    bool split = true;
    if (!started_) split = true;                                                         // GB1
    else if (pg == CR && g == LF) split = false;                                         // GB3
    else if (isControlLike(pg) || isControlLike(g)) split = true;                        // GB4, GB5
    else if (pg == L && (g == L || g == V || g == LV || g == LVT)) split = false;        // GB6
    else if ((pg == LV || pg == V) && (g == V || g == T)) split = false;                 // GB7
    else if ((pg == LVT || pg == T) && g == T) split = false;                            // GB8
    else if (g == Extend || g == ZWJ || g == SpacingMark || pg == Prepend) split = false; // GB9, GB9a, GB9b
    else if (conjunct_ == 2 && (p & kConsonant)) split = false;                          // GB9c
    else if (pictographic_ == 2 && (p & kExtendedPictographic)) split = false;           // GB11
    else if (pg == RegionalIndicator && g == RegionalIndicator) split = regional_ % 2 == 0;  // GB12, GB13

    started_ = true;
    previous_ = p;
    regional_ = g == RegionalIndicator ? regional_ + 1 : 0;
    if (p & kExtendedPictographic) pictographic_ = 1;
    else if (g == Extend && pictographic_ == 1) pictographic_ = 1;
    else if (g == ZWJ && pictographic_ == 1) pictographic_ = 2;
    else pictographic_ = 0;
    if (p & kConsonant) conjunct_ = 1;
    else if ((p & kLinker) && conjunct_ != 0) conjunct_ = 2;
    else if (!(p & kConjunctExtend)) conjunct_ = 0;
    return split;
}

bool isLetterOrNumber(uint32_t scalar) { return inRanges(kAlphaNumericRanges, scalar); }

bool isWhitespace(uint32_t scalar) { return inRanges(kWhitespaceRanges, scalar); }

std::string trimWhitespace(std::string_view s) {
    size_t begin = 0;
    while (begin < s.size()) {
        size_t next = begin;
        if (!isWhitespace(decodeScalar(s, next))) break;
        begin = next;
    }
    size_t end = s.size();
    while (end > begin) {
        size_t start = end - 1;
        while (start > begin && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
        size_t next = start;
        if (!isWhitespace(decodeScalar(s, next)) || next != end) break;
        end = start;
    }
    return std::string(s.substr(begin, end - begin));
}

std::string lowercased(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const size_t start = i;
        const uint32_t u = decodeScalar(s, i);
        if (u == 0xFFFD && i - start == 1 && static_cast<unsigned char>(s[start]) >= 0x80) {
            out += s[start];  // keep malformed bytes as they are
            continue;
        }
        appendLower(out, u);
    }
    return out;
}

int caseInsensitiveCompare(std::string_view a, std::string_view b) {
    // UTF-8 byte order is scalar order.
    const int c = lowercased(a).compare(lowercased(b));
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

}  // namespace bt::unicode
