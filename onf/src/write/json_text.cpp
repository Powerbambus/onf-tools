#include "json_text.h"

#include <utility>

namespace onf {

namespace {

void write_string(std::string& out, const std::string& text) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (byte < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[byte >> 4]);
                    out.push_back(kHex[byte & 0x0F]);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void write_value(std::string& out, const JsonValue& value, unsigned depth) {
    const std::string indent(4 * (depth + 1), ' ');
    const std::string closing_indent(4 * depth, ' ');

    if (value.is_null()) {
        out += "null";
    } else if (const bool* b = value.get_bool()) {
        out += *b ? "true" : "false";
    } else if (const std::int64_t* n = value.get_integer()) {
        out += std::to_string(*n);
    } else if (const std::string* s = value.get_string()) {
        write_string(out, *s);
    } else if (const JsonValue::Array* array = value.get_array()) {
        if (array->empty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        for (std::size_t i = 0; i < array->size(); ++i) {
            out += indent;
            write_value(out, (*array)[i], depth + 1);
            out += i + 1 < array->size() ? ",\n" : "\n";
        }
        out += closing_indent + "]";
    } else if (const JsonValue::Object* object = value.get_object()) {
        if (object->empty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        for (std::size_t i = 0; i < object->size(); ++i) {
            out += indent;
            write_string(out, (*object)[i].key);
            out += ": ";
            write_value(out, (*object)[i].value, depth + 1);
            out += i + 1 < object->size() ? ",\n" : "\n";
        }
        out += closing_indent + "}";
    }
}

}  // namespace

std::string serialize_json(const JsonValue& value) {
    std::string out;
    write_value(out, value, 0);
    out.push_back('\n');
    return out;
}

JsonValue json_object() { return JsonValue{JsonValue::Object{}}; }
JsonValue json_array() { return JsonValue{JsonValue::Array{}}; }
JsonValue json_string(std::string text) { return JsonValue{std::move(text)}; }
JsonValue json_integer(std::int64_t value) { return JsonValue{value}; }
JsonValue json_bool(bool value) { return JsonValue{value}; }

void json_add(JsonValue& object, std::string key, JsonValue value) {
    std::get<JsonValue::Object>(object.data).push_back(JsonValue::Member{std::move(key), std::move(value)});
}

void json_push(JsonValue& array, JsonValue value) { std::get<JsonValue::Array>(array.data).push_back(std::move(value)); }

}  // namespace onf
