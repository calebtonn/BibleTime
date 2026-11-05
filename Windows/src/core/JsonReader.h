// Internal to the core: the cursor-based JSON reader behind json::parse(). Translation.cpp uses it directly to
// stream the multi-megabyte "verses" array into Verse records without building a DOM for it.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "Json.h"

namespace bt::json::detail {

class Reader {
public:
    /// Skips a leading UTF-8 byte order mark.
    explicit Reader(std::string_view text);

    /// Skip whitespace and return the next byte, or '\0' at the end of input.
    char peek();
    /// Skip whitespace; consume `c` when it is next. Returns whether it was.
    bool consume(char c);
    /// Skip whitespace; consume `c` or fail.
    void expect(char c);
    /// Skip trailing whitespace and fail unless the input is exhausted.
    void finish();

    /// Parse any value. `depth` guards against stack exhaustion on hostile input.
    Value parseValue(int depth = 0);
    /// Parse a string (the next token must be one) into `out` (replacing its contents).
    void parseString(std::string& out);
    /// Parse a number token; the next token must be one.
    Value parseNumber();

    [[noreturn]] void fail(const char* what) const;

    static constexpr int maxDepth = 512;

private:
    void skipWhitespace();
    void parseLiteral(const char* word);
    unsigned parseHex4();

    const char* begin_;
    const char* p_;
    const char* end_;
};

}  // namespace bt::json::detail
