#pragma once

#include <string>

#include "onf/read/json.h"

namespace onf {

// JSON text of a value, indented with four spaces like the examples of the specification. Strings are written as
// UTF-8; only quotes, backslashes and control characters are escaped, so no surrogate escapes can appear.
std::string serialize_json(const JsonValue& value);

// Builders for the model-to-JSON code.
JsonValue json_object();
JsonValue json_array();
JsonValue json_string(std::string text);
JsonValue json_integer(std::int64_t value);
JsonValue json_bool(bool value);
void json_add(JsonValue& object, std::string key, JsonValue value);
void json_push(JsonValue& array, JsonValue value);

}  // namespace onf
