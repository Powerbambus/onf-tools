#include "onf/read/json.h"

#include <array>
#include <limits>
#include <unordered_set>

#include "onf/limits.h"
#include "onf/primitives/utf8.h"

namespace onf {

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    const Object* object = get_object();
    if (object == nullptr) return nullptr;
    for (const auto& member : *object) {
        if (member.key == key) return &member.value;
    }
    return nullptr;
}

namespace {

constexpr std::uint32_t kNoStringLimit = std::numeric_limits<std::uint32_t>::max();

class Parser {
public:
    Parser(std::span<const std::uint8_t> input, JsonOptions options) : in_(input), options_(options) {}

    Result<JsonValue, JsonError> run() {
        if (in_.size() >= 3 && in_[0] == 0xEF && in_[1] == 0xBB && in_[2] == 0xBF) {
            return unexpected(JsonError{ErrorCode::JsonBom, "the file starts with a byte order mark", 0});
        }
        JsonValue value;
        skip_whitespace();
        if (!parse_value(value, 0)) return unexpected(std::move(error_));
        skip_whitespace();
        if (pos_ != in_.size()) {
            fail(ErrorCode::JsonSyntax, "unexpected data after the JSON value");
            return unexpected(std::move(error_));
        }
        return value;
    }

private:
    struct Frame {
        bool is_object = false;
        std::string key;
    };

    bool fail(ErrorCode code, std::string message) {
        error_ = JsonError{code, std::move(message), pos_};
        return false;
    }

    void skip_whitespace() {
        while (pos_ < in_.size() && (in_[pos_] == ' ' || in_[pos_] == '\t' || in_[pos_] == '\n' || in_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (pos_ < in_.size() && in_[pos_] == static_cast<std::uint8_t>(expected)) {
            ++pos_;
            return true;
        }
        return false;
    }

    // depth is the nesting depth of the container that holds the value (0 at top level).
    bool parse_value(JsonValue& out, unsigned depth) {
        if (pos_ >= in_.size()) return fail(ErrorCode::JsonSyntax, "unexpected end of input");

        const std::uint8_t c = in_[pos_];
        switch (c) {
            case '{': return parse_object(out, depth + 1);
            case '[': return parse_array(out, depth + 1);
            case '"': {
                std::string text;
                if (!parse_string(text, string_limit(depth))) return false;
                out.data = std::move(text);
                return true;
            }
            case 't': return parse_literal("true", JsonValue{true}, out);
            case 'f': return parse_literal("false", JsonValue{false}, out);
            case 'n': return parse_literal("null", JsonValue{}, out);
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);
                return fail(ErrorCode::JsonSyntax, "unexpected character");
        }
    }

    bool parse_literal(std::string_view word, JsonValue value, JsonValue& out) {
        for (char expected : word) {
            if (!consume(expected)) return fail(ErrorCode::JsonSyntax, "invalid literal");
        }
        out = std::move(value);
        return true;
    }

    bool parse_number(JsonValue& out) {
        const bool negative = consume('-');
        if (pos_ >= in_.size() || in_[pos_] < '0' || in_[pos_] > '9') {
            return fail(ErrorCode::JsonSyntax, "a minus sign must be followed by a digit");
        }

        std::uint64_t magnitude = 0;
        bool too_large = false;
        if (in_[pos_] == '0') {
            ++pos_;
            if (pos_ < in_.size() && in_[pos_] >= '0' && in_[pos_] <= '9') {
                return fail(ErrorCode::JsonSyntax, "numbers must not have leading zeros");
            }
            if (negative && !is_fraction_or_exponent_next()) {
                return fail(ErrorCode::JsonNonIntegerNumber, "negative zero is not an integer");
            }
        } else {
            while (pos_ < in_.size() && in_[pos_] >= '0' && in_[pos_] <= '9') {
                if (!too_large) {
                    magnitude = magnitude * 10 + (in_[pos_] - '0');
                    too_large = magnitude > static_cast<std::uint64_t>(limits::kJsonMaxInteger);
                }
                ++pos_;
            }
        }

        if (is_fraction_or_exponent_next()) {
            return fail(ErrorCode::JsonNonIntegerNumber, "numbers must be integers without fraction or exponent");
        }
        if (too_large) return fail(ErrorCode::JsonIntegerOutOfRange, "integer outside +-(2^53 - 1)");

        const auto value = static_cast<std::int64_t>(magnitude);
        out.data = negative ? -value : value;
        return true;
    }

    bool is_fraction_or_exponent_next() const {
        return pos_ < in_.size() && (in_[pos_] == '.' || in_[pos_] == 'e' || in_[pos_] == 'E');
    }

    bool parse_array(JsonValue& out, unsigned depth) {
        if (depth > limits::kJsonMaxDepth) return fail(ErrorCode::JsonNestingTooDeep, "nesting is too deep");
        enter_frame(depth, false);
        ++pos_;

        JsonValue::Array items;
        skip_whitespace();
        if (consume(']')) {
            out.data = std::move(items);
            return true;
        }
        while (true) {
            skip_whitespace();
            JsonValue item;
            if (!parse_value(item, depth)) return false;
            items.push_back(std::move(item));

            skip_whitespace();
            if (consume(',')) continue;
            if (consume(']')) break;
            return fail(ErrorCode::JsonSyntax, "expected ',' or ']'");
        }
        out.data = std::move(items);
        return true;
    }

    bool parse_object(JsonValue& out, unsigned depth) {
        if (depth > limits::kJsonMaxDepth) return fail(ErrorCode::JsonNestingTooDeep, "nesting is too deep");
        enter_frame(depth, true);
        ++pos_;

        JsonValue::Object members;
        std::unordered_set<std::string> keys;
        skip_whitespace();
        if (consume('}')) {
            out.data = std::move(members);
            return true;
        }
        while (true) {
            skip_whitespace();
            if (pos_ >= in_.size() || in_[pos_] != '"') return fail(ErrorCode::JsonSyntax, "expected a string as key");

            std::string key;
            if (!parse_string(key, kNoStringLimit)) return false;
            if (!keys.insert(key).second) return fail(ErrorCode::JsonDuplicateKey, "duplicate key \"" + key + "\"");
            set_frame_key(depth, key);

            skip_whitespace();
            if (!consume(':')) return fail(ErrorCode::JsonSyntax, "expected ':' after a key");
            skip_whitespace();

            JsonValue member;
            if (!parse_value(member, depth)) return false;
            members.emplace_back(std::move(key), std::move(member));

            skip_whitespace();
            if (consume(',')) continue;
            if (consume('}')) break;
            return fail(ErrorCode::JsonSyntax, "expected ',' or '}'");
        }
        out.data = std::move(members);
        return true;
    }

    // Only the first three levels are tracked: that is enough to recognise text_objects[].content.
    void enter_frame(unsigned depth, bool is_object) {
        if (depth < frames_.size()) frames_[depth] = Frame{is_object, {}};
    }

    void set_frame_key(unsigned depth, const std::string& key) {
        if (depth < frames_.size()) frames_[depth].key = key;
    }

    std::uint32_t string_limit(unsigned depth) const {
        const bool is_text_content = options_.long_text_content && depth == 3 && frames_[3].is_object &&
                                     frames_[3].key == "content" && !frames_[2].is_object && frames_[1].is_object &&
                                     frames_[1].key == "text_objects";
        return is_text_content ? limits::kJsonMaxContentCodePoints : limits::kJsonMaxStringCodePoints;
    }

    bool parse_string(std::string& out, std::uint32_t max_code_points) {
        ++pos_;  // opening quote
        std::uint32_t count = 0;

        while (true) {
            if (pos_ >= in_.size()) return fail(ErrorCode::JsonSyntax, "unterminated string");

            const std::uint8_t c = in_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) return fail(ErrorCode::JsonSyntax, "control character in string");

            if (++count > max_code_points) return fail(ErrorCode::JsonStringTooLong, "string is too long");

            if (c == '\\') {
                if (!parse_escape(out)) return false;
            } else if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++pos_;
            } else if (!copy_utf8_sequence(out)) {
                return false;
            }
        }
    }

    bool parse_escape(std::string& out) {
        ++pos_;  // backslash
        if (pos_ >= in_.size()) return fail(ErrorCode::JsonSyntax, "unterminated escape sequence");

        const std::uint8_t c = in_[pos_++];
        switch (c) {
            case '"': out.push_back('"'); return true;
            case '\\': out.push_back('\\'); return true;
            case '/': out.push_back('/'); return true;
            case 'b': out.push_back('\b'); return true;
            case 'f': out.push_back('\f'); return true;
            case 'n': out.push_back('\n'); return true;
            case 'r': out.push_back('\r'); return true;
            case 't': out.push_back('\t'); return true;
            case 'u': return parse_unicode_escape(out);
            default: return fail(ErrorCode::JsonSyntax, "invalid escape sequence");
        }
    }

    bool parse_unicode_escape(std::string& out) {
        std::uint32_t code = 0;
        if (!read_hex4(code)) return false;

        if (code >= 0xDC00 && code <= 0xDFFF) {
            return fail(ErrorCode::JsonUnpairedSurrogate, "low surrogate without a preceding high surrogate");
        }
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (!(consume('\\') && consume('u'))) {
                return fail(ErrorCode::JsonUnpairedSurrogate, "high surrogate without a following low surrogate");
            }
            std::uint32_t low = 0;
            if (!read_hex4(low)) return false;
            if (low < 0xDC00 || low > 0xDFFF) {
                return fail(ErrorCode::JsonUnpairedSurrogate, "high surrogate without a following low surrogate");
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        }
        append_utf8(out, code);
        return true;
    }

    bool read_hex4(std::uint32_t& value) {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= in_.size()) return fail(ErrorCode::JsonSyntax, "incomplete \\u escape");
            const std::uint8_t c = in_[pos_];
            std::uint32_t digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return fail(ErrorCode::JsonSyntax, "invalid hex digit in \\u escape");
            value = value * 16 + digit;
            ++pos_;
        }
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool copy_utf8_sequence(std::string& out) {
        const std::size_t length = utf8_sequence_length(in_.subspan(pos_));
        if (length == 0) return fail(ErrorCode::JsonInvalidUtf8, "invalid UTF-8 sequence");

        for (std::size_t i = 0; i < length; ++i) out.push_back(static_cast<char>(in_[pos_ + i]));
        pos_ += length;
        return true;
    }

    std::span<const std::uint8_t> in_;
    JsonOptions options_;
    std::size_t pos_ = 0;
    std::array<Frame, 4> frames_;
    JsonError error_{ErrorCode::JsonSyntax, {}, 0};
};

}  // namespace

Result<JsonValue, JsonError> parse_json(std::span<const std::uint8_t> bytes, JsonOptions options) {
    return Parser(bytes, options).run();
}

Result<JsonValue, JsonError> parse_json(std::string_view text, JsonOptions options) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(text.data());
    return parse_json(std::span<const std::uint8_t>(begin, text.size()), options);
}

}  // namespace onf
