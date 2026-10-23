#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "onf/errors.h"
#include "onf/result.h"

namespace onf {

// Parsed JSON restricted to what ONF uses: numbers are integers only (spec 2.3.5).
class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    struct Member;  // completed below; a vector of an incomplete type is allowed, a vector of pairs of it is not
    using Object = std::vector<Member>;  // in file order, keys are unique

    std::variant<std::monostate, bool, std::int64_t, std::string, Array, Object> data;

    bool is_null() const noexcept { return std::holds_alternative<std::monostate>(data); }
    const bool* get_bool() const noexcept { return std::get_if<bool>(&data); }
    const std::int64_t* get_integer() const noexcept { return std::get_if<std::int64_t>(&data); }
    const std::string* get_string() const noexcept { return std::get_if<std::string>(&data); }
    const Array* get_array() const noexcept { return std::get_if<Array>(&data); }
    const Object* get_object() const noexcept { return std::get_if<Object>(&data); }

    // nullptr if this is not an object or the key is absent.
    const JsonValue* find(std::string_view key) const noexcept;
};

struct JsonValue::Member {
    std::string key;
    JsonValue value;
};

struct JsonError {
    ErrorCode code;
    std::string message;
    std::uint64_t offset;
};

struct JsonOptions {
    // Applies the larger string limit (spec 12.6) to text_objects[].content. Only for meta.json.
    bool long_text_content = false;
};

// Strict parser for manifest.json and meta.json: structural limits of spec 12.6, no duplicate keys,
// no BOM, valid UTF-8, no unpaired surrogate escapes, integers within +-(2^53 - 1).
Result<JsonValue, JsonError> parse_json(std::span<const std::uint8_t> bytes, JsonOptions options = {});
Result<JsonValue, JsonError> parse_json(std::string_view text, JsonOptions options = {});

}  // namespace onf
