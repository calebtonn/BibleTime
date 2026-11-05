// Unicode text handling that must match Swift's String exactly: extended grapheme clusters (String.count,
// Character), Character.isLetter / isNumber, CharacterSet.whitespaces and String.lowercased().
// The property tables (GraphemeTables.inc) are generated from the UCD by Windows/tools/gen_unicode_tables.py.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bt::unicode {

/// Decode one scalar from UTF-8 and advance `i`. Malformed bytes decode as U+FFFD one byte at a time.
uint32_t decodeScalar(std::string_view s, size_t& i);
void appendUTF8(std::string& out, uint32_t scalar);

/// UAX #29 extended grapheme cluster segmentation (GB3-GB13 with GB9c), as Swift's String uses it.
/// Feed the scalars of a string in order; each call says whether a new cluster starts at that scalar.
class GraphemeBreaker {
public:
    bool startsCluster(uint32_t scalar);

private:
    bool started_ = false;
    uint8_t previous_ = 0;      // properties of the previous scalar
    size_t regional_ = 0;       // regional indicators immediately before
    uint8_t pictographic_ = 0;  // 1: ExtPict Extend*, 2: ExtPict Extend* ZWJ
    uint8_t conjunct_ = 0;      // 1: InCB Consonant [Extend Linker]*, 2: the same with at least one Linker
};

/// Swift Character.isLetter || Character.isNumber for a cluster starting with `scalar` (Alphabetic, or a
/// Numeric_Type other than None).
bool isLetterOrNumber(uint32_t scalar);
/// Member of CharacterSet.whitespaces (general category Zs, or tab).
bool isWhitespace(uint32_t scalar);

/// String.trimmingCharacters(in: .whitespaces).
std::string trimWhitespace(std::string_view utf8);
/// String.lowercased(): the full, context-free Lowercase_Mapping of every scalar.
std::string lowercased(std::string_view utf8);
/// Lowercase both, then compare by scalar value: negative, zero or positive.
int caseInsensitiveCompare(std::string_view a, std::string_view b);

}  // namespace bt::unicode
