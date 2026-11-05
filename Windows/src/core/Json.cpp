// JSON reader and writer. Hand-written for speed: the bundled translations are ~3 MB each and are parsed every
// time the screen saver starts, so the hot paths (strings without escapes, small integers) avoid per-byte work and
// heap churn. Locale independent: numbers never go through the C locale's decimal separator unchecked.
#include "Json.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "JsonReader.h"

namespace bt::json {

// MARK: - Value

Value Value::makeBool(bool b) {
    Value v;
    v.type_ = Type::Bool;
    v.bool_ = b;
    return v;
}

Value Value::makeNumber(double d) {
    Value v;
    v.type_ = Type::Number;
    v.number_ = d;
    // Integral and inside int64 (2^63 is not representable as int64, hence the strict upper bound).
    if (std::isfinite(d) && std::floor(d) == d && d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
        v.integral_ = true;
        v.int_ = static_cast<int64_t>(d);
    }
    return v;
}

Value Value::makeInt(int64_t i) {
    Value v;
    v.type_ = Type::Number;
    v.number_ = static_cast<double>(i);
    v.integral_ = true;
    v.int_ = i;
    return v;
}

Value Value::makeString(std::string s) {
    Value v;
    v.type_ = Type::String;
    v.string_ = std::move(s);
    return v;
}

Value Value::makeArray(std::vector<Value> items) {
    Value v;
    v.type_ = Type::Array;
    v.array_ = std::move(items);
    return v;
}

Value Value::makeObject(std::vector<std::pair<std::string, Value>> members) {
    Value v;
    v.type_ = Type::Object;
    v.object_ = std::move(members);
    return v;
}

const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : object_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

void Value::set(std::string key, Value v) {
    if (type_ != Type::Object) throw std::logic_error("json::Value::set on a non-object");
    for (auto& member : object_) {
        if (member.first == key) {
            member.second = std::move(v);
            return;
        }
    }
    object_.emplace_back(std::move(key), std::move(v));
}

// MARK: - Reader

namespace detail {

namespace {

constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }

/// Exact powers of ten representable as doubles (Clinger's fast path).
constexpr double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                             1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

/// strtod on a copy of the token, with '.' swapped for the C locale's decimal separator if it differs.
double strtodLocaleIndependent(const char* first, const char* last) {
    std::string token(first, last);
    const char* point = std::localeconv()->decimal_point;
    if (point && point[0] != '.' && point[0] != '\0') {
        std::string fixed;
        for (char c : token) {
            if (c == '.') fixed += point;
            else fixed += c;
        }
        token = std::move(fixed);
    }
    return std::strtod(token.c_str(), nullptr);
}

void appendUTF8(std::string& out, uint32_t scalar) {
    if (scalar < 0x80) {
        out += static_cast<char>(scalar);
    } else if (scalar < 0x800) {
        out += static_cast<char>(0xC0 | (scalar >> 6));
        out += static_cast<char>(0x80 | (scalar & 0x3F));
    } else if (scalar < 0x10000) {
        out += static_cast<char>(0xE0 | (scalar >> 12));
        out += static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (scalar & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (scalar >> 18));
        out += static_cast<char>(0x80 | ((scalar >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (scalar & 0x3F));
    }
}

/// Length of the well-formed UTF-8 sequence starting at `p` (lead byte >= 0x80), or 0 when malformed
/// (overlong forms, surrogates, values above U+10FFFF and truncated sequences are all rejected).
size_t utf8SequenceLength(const unsigned char* p, const unsigned char* end) {
    const unsigned char c = p[0];
    size_t n;
    unsigned char lo = 0x80, hi = 0xBF;  // allowed range of the second byte
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
    } else {
        return 0;
    }
    if (static_cast<size_t>(end - p) < n) return 0;
    if (p[1] < lo || p[1] > hi) return 0;
    for (size_t i = 2; i < n; ++i) {
        if ((p[i] & 0xC0) != 0x80) return 0;
    }
    return n;
}

}  // namespace

Reader::Reader(std::string_view text) : begin_(text.data()), p_(text.data()), end_(text.data() + text.size()) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        p_ += 3;
    }
}

void Reader::fail(const char* what) const {
    throw ParseError(std::string("JSON parse error at offset ") + std::to_string(p_ - begin_) + ": " + what);
}

void Reader::skipWhitespace() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t')) ++p_;
}

char Reader::peek() {
    skipWhitespace();
    return p_ < end_ ? *p_ : '\0';
}

bool Reader::consume(char c) {
    skipWhitespace();
    if (p_ < end_ && *p_ == c) {
        ++p_;
        return true;
    }
    return false;
}

void Reader::expect(char c) {
    if (!consume(c)) {
        char message[] = "expected 'x'";
        message[10] = c;
        fail(message);
    }
}

void Reader::finish() {
    skipWhitespace();
    if (p_ != end_) fail("unexpected trailing characters");
}

void Reader::parseLiteral(const char* word) {
    const size_t n = std::strlen(word);
    if (static_cast<size_t>(end_ - p_) < n || std::memcmp(p_, word, n) != 0) fail("invalid literal");
    p_ += n;
}

unsigned Reader::parseHex4() {
    if (end_ - p_ < 4) fail("truncated \\u escape");
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = *p_++;
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
        else fail("invalid \\u escape");
    }
    return value;
}

void Reader::parseString(std::string& out) {
    skipWhitespace();
    if (p_ >= end_ || *p_ != '"') fail("expected a string");
    ++p_;
    out.clear();
    for (;;) {
        // Copy the longest run of plain ASCII in one append.
        const char* run = p_;
        while (p_ < end_) {
            const auto c = static_cast<unsigned char>(*p_);
            if (c == '"' || c == '\\' || c < 0x20 || c >= 0x80) break;
            ++p_;
        }
        if (p_ != run) out.append(run, static_cast<size_t>(p_ - run));
        if (p_ >= end_) fail("unterminated string");
        const auto c = static_cast<unsigned char>(*p_);
        if (c == '"') {
            ++p_;
            return;
        }
        if (c < 0x20) fail("control character in string");
        if (c >= 0x80) {
            const size_t n = utf8SequenceLength(reinterpret_cast<const unsigned char*>(p_),
                                                reinterpret_cast<const unsigned char*>(end_));
            if (n == 0) fail("invalid UTF-8 in string");
            out.append(p_, n);
            p_ += n;
            continue;
        }
        // Backslash escape.
        ++p_;
        if (p_ >= end_) fail("unterminated escape");
        switch (*p_++) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t scalar = parseHex4();
                if (scalar >= 0xD800 && scalar <= 0xDBFF) {
                    // A high surrogate must be followed by an escaped low surrogate.
                    if (end_ - p_ < 6 || p_[0] != '\\' || p_[1] != 'u') fail("unpaired surrogate in \\u escape");
                    p_ += 2;
                    const uint32_t low = parseHex4();
                    if (low < 0xDC00 || low > 0xDFFF) fail("unpaired surrogate in \\u escape");
                    scalar = 0x10000 + ((scalar - 0xD800) << 10) + (low - 0xDC00);
                } else if (scalar >= 0xDC00 && scalar <= 0xDFFF) {
                    fail("unpaired surrogate in \\u escape");
                }
                appendUTF8(out, scalar);
                break;
            }
            default: --p_; fail("invalid escape");
        }
    }
}

Value Reader::parseNumber() {
    skipWhitespace();
    const char* start = p_;
    bool negative = false;
    if (p_ < end_ && *p_ == '-') {
        negative = true;
        ++p_;
    }
    if (p_ >= end_ || !isDigit(*p_)) fail("invalid number");

    // Integer part. Accumulate up to 19 significant digits exactly; count the rest as a decimal exponent.
    uint64_t mantissa = 0;
    int digits = 0;        // significant digits accumulated into mantissa
    int dropped = 0;       // significant digits that did not fit
    int exponent10 = 0;    // adjustment to the decimal exponent
    bool integerToken = true;
    if (*p_ == '0') {
        ++p_;
        if (p_ < end_ && isDigit(*p_)) fail("leading zero in number");
    } else {
        while (p_ < end_ && isDigit(*p_)) {
            if (digits < 19) {
                mantissa = mantissa * 10 + static_cast<uint64_t>(*p_ - '0');
                ++digits;
            } else {
                ++dropped;
                ++exponent10;
            }
            ++p_;
        }
    }
    if (p_ < end_ && *p_ == '.') {
        integerToken = false;
        ++p_;
        if (p_ >= end_ || !isDigit(*p_)) fail("invalid number");
        while (p_ < end_ && isDigit(*p_)) {
            if (digits < 19) {
                if (mantissa != 0 || *p_ != '0') {
                    mantissa = mantissa * 10 + static_cast<uint64_t>(*p_ - '0');
                    ++digits;
                }
                --exponent10;
            } else if (*p_ != '0') {
                ++dropped;
            }
            ++p_;
        }
    }
    if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
        integerToken = false;
        ++p_;
        bool expNegative = false;
        if (p_ < end_ && (*p_ == '+' || *p_ == '-')) {
            expNegative = *p_ == '-';
            ++p_;
        }
        if (p_ >= end_ || !isDigit(*p_)) fail("invalid number exponent");
        int e = 0;
        while (p_ < end_ && isDigit(*p_)) {
            if (e < 100000) e = e * 10 + (*p_ - '0');
            ++p_;
        }
        exponent10 += expNegative ? -e : e;
    }

    if (integerToken && dropped == 0) {
        // Exact int64 when it fits.
        const uint64_t limit = negative ? (uint64_t{1} << 63) : (uint64_t{1} << 63) - 1;
        if (mantissa <= limit) {
            const int64_t value = negative ? static_cast<int64_t>(0 - mantissa) : static_cast<int64_t>(mantissa);
            return Value::makeInt(value);
        }
    }
    double d;
    if (dropped == 0 && mantissa <= (uint64_t{1} << 53) && exponent10 >= -22 && exponent10 <= 22) {
        d = static_cast<double>(mantissa);
        d = exponent10 < 0 ? d / kPow10[-exponent10] : d * kPow10[exponent10];
        if (negative) d = -d;
    } else {
        d = strtodLocaleIndependent(start, p_);
    }
    if (!std::isfinite(d)) fail("number out of range");
    return Value::makeNumber(d);
}

Value Reader::parseValue(int depth) {
    if (depth > maxDepth) fail("nesting too deep");
    switch (peek()) {
        case '{': {
            ++p_;
            std::vector<std::pair<std::string, Value>> members;
            if (consume('}')) return Value::makeObject(std::move(members));
            do {
                std::string key;
                parseString(key);
                expect(':');
                Value v = parseValue(depth + 1);
                members.emplace_back(std::move(key), std::move(v));
            } while (consume(','));
            expect('}');
            return Value::makeObject(std::move(members));
        }
        case '[': {
            ++p_;
            std::vector<Value> items;
            if (consume(']')) return Value::makeArray(std::move(items));
            do {
                items.push_back(parseValue(depth + 1));
            } while (consume(','));
            expect(']');
            return Value::makeArray(std::move(items));
        }
        case '"': {
            std::string s;
            parseString(s);
            return Value::makeString(std::move(s));
        }
        case 't': parseLiteral("true"); return Value::makeBool(true);
        case 'f': parseLiteral("false"); return Value::makeBool(false);
        case 'n': parseLiteral("null"); return Value();
        case '\0':
            if (p_ >= end_) fail("unexpected end of input");
            fail("unexpected character");
        default:
            if (*p_ == '-' || isDigit(*p_)) return parseNumber();
            fail("unexpected character");
    }
}

}  // namespace detail

Value parse(std::string_view text) {
    detail::Reader reader(text);
    Value v = reader.parseValue();
    reader.finish();
    return v;
}

// MARK: - Writer

namespace {

void writeString(std::string& out, const std::string& s) {
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += ch;  // UTF-8 passes through unchanged
                }
        }
    }
    out += '"';
}

/// Shortest "%.Ng" representation that reads back to the same double, with a '.' decimal point.
std::string formatDouble(double d) {
    if (!std::isfinite(d)) return "null";
    char buffer[40];
    for (int precision = 15; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, d);
        if (precision == 17 || std::strtod(buffer, nullptr) == d) break;
    }
    std::string s(buffer);
    const char* point = std::localeconv()->decimal_point;
    if (point && point[0] != '.' && point[0] != '\0') {
        const size_t at = s.find(point);
        if (at != std::string::npos) s.replace(at, std::strlen(point), ".");
    }
    return s;
}

void write(std::string& out, const Value& v, int indent, int level) {
    const auto newline = [&](int lvl) {
        if (indent <= 0) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * lvl), ' ');
    };
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number:
            if (v.isInteger()) out += std::to_string(v.asInt());
            else out += formatDouble(v.asDouble());
            break;
        case Value::Type::String: writeString(out, v.asString()); break;
        case Value::Type::Array: {
            if (v.items().empty()) {
                out += "[]";
                break;
            }
            out += '[';
            bool first = true;
            for (const Value& item : v.items()) {
                if (!first) out += ',';
                first = false;
                newline(level + 1);
                write(out, item, indent, level + 1);
            }
            newline(level);
            out += ']';
            break;
        }
        case Value::Type::Object: {
            if (v.members().empty()) {
                out += "{}";
                break;
            }
            out += '{';
            bool first = true;
            for (const auto& member : v.members()) {
                if (!first) out += ',';
                first = false;
                newline(level + 1);
                writeString(out, member.first);
                out += indent > 0 ? ": " : ":";
                write(out, member.second, indent, level + 1);
            }
            newline(level);
            out += '}';
            break;
        }
    }
}

}  // namespace

std::string serialize(const Value& v, int indent) {
    std::string out;
    write(out, v, indent, 0);
    return out;
}

}  // namespace bt::json
