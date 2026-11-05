// Minimal, dependency-free JSON reader and writer for BibleTime's resource files and settings.
// Accepts RFC 8259 JSON (UTF-8). Numbers keep both a double and, when integral, an exact int64.
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bt::json {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value makeBool(bool b);
    static Value makeNumber(double d);
    static Value makeInt(int64_t i);
    static Value makeString(std::string s);
    static Value makeArray(std::vector<Value> items = {});
    static Value makeObject(std::vector<std::pair<std::string, Value>> members = {});

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    /// True for a number with no fractional part that fits in int64 (e.g. 3, -7, 1e2).
    bool isInteger() const { return type_ == Type::Number && integral_; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const { return bool_; }
    double asDouble() const { return number_; }
    int64_t asInt() const { return int_; }
    const std::string& asString() const { return string_; }
    const std::vector<Value>& items() const { return array_; }
    std::vector<Value>& items() { return array_; }
    const std::vector<std::pair<std::string, Value>>& members() const { return object_; }
    std::vector<std::pair<std::string, Value>>& members() { return object_; }

    /// Object member lookup (first match), or nullptr when absent or not an object.
    const Value* find(std::string_view key) const;
    /// Set (replace or append) an object member. The value must be an object.
    void set(std::string key, Value v);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    bool integral_ = false;
    double number_ = 0;
    int64_t int_ = 0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;
};

/// Parse a complete JSON document. Throws ParseError on malformed input or trailing garbage.
/// A leading UTF-8 byte order mark is skipped.
Value parse(std::string_view text);

/// Serialize. `indent` > 0 pretty-prints with that many spaces per level.
std::string serialize(const Value& v, int indent = 0);

}  // namespace bt::json
