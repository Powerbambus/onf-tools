#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "onf/errors.h"
#include "onf/limits.h"
#include "onf/read/json.h"

using namespace onf;

namespace {

std::string utf8(const char8_t* text) { return std::string(reinterpret_cast<const char*>(text)); }

#define U8(literal) utf8(u8##literal)

std::string esc(const std::string& hex4) { return std::string(1, '\\') + "u" + hex4; }

std::string utf8_of(std::uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

Result<JsonValue, JsonError> parse(std::string_view text, JsonOptions options = {}) { return parse_json(text, options); }

JsonValue parse_ok(std::string_view text) {
    auto result = parse(text);
    REQUIRE_MESSAGE(result.has_value(), "unexpected error for [" << text << "]: " << result.error().message);
    return std::move(result).value();
}

ErrorCode error_of(std::string_view text, JsonOptions options = {}) {
    const auto result = parse(text, options);
    REQUIRE_MESSAGE(!result.has_value(), "unexpectedly accepted: " << text);
    return result.error().code;
}

std::string quoted(const std::string& inner) { return "\"" + inner + "\""; }

std::string nested(char open, char close, unsigned levels) {
    return std::string(levels, open) + std::string(levels, close);
}

std::string nested_objects(unsigned levels) {
    std::string text;
    for (unsigned i = 0; i < levels; ++i) text += "{\"a\":";
    text += "1";
    text += std::string(levels, '}');
    return text;
}

std::string repeat(const std::string& unit, std::size_t times) {
    std::string out;
    out.reserve(unit.size() * times);
    for (std::size_t i = 0; i < times; ++i) out += unit;
    return out;
}

}  // namespace

TEST_CASE("scalars at top level") {
    CHECK(parse_ok("null").is_null());
    CHECK(*parse_ok("true").get_bool() == true);
    CHECK(*parse_ok("false").get_bool() == false);
    CHECK(*parse_ok("0").get_integer() == 0);
    CHECK(*parse_ok("-17").get_integer() == -17);
    CHECK(*parse_ok("\"hi\"").get_string() == "hi");
}

TEST_CASE("objects and arrays keep order and are searchable") {
    const JsonValue v = parse_ok(R"( { "b" : [1, 2, {"x": null}], "a" : "s", "c": {} , "d": [] } )");
    const auto* object = v.get_object();
    REQUIRE(object != nullptr);
    REQUIRE(object->size() == 4);
    CHECK((*object)[0].first == "b");
    CHECK((*object)[1].first == "a");

    CHECK(*v.find("a")->get_string() == "s");
    CHECK(v.find("missing") == nullptr);
    CHECK(v.find("c")->get_object()->empty());
    CHECK(v.find("d")->get_array()->empty());

    const auto* array = v.find("b")->get_array();
    REQUIRE(array->size() == 3);
    CHECK(*(*array)[1].get_integer() == 2);
    CHECK((*array)[2].find("x")->is_null());
    CHECK(JsonValue{}.find("a") == nullptr);
}

TEST_CASE("typed accessors return null for other types") {
    const JsonValue t = parse_ok("true");
    CHECK(t.get_integer() == nullptr);
    CHECK(t.get_string() == nullptr);
    CHECK(parse_ok("1").get_bool() == nullptr);
    CHECK(parse_ok("\"1\"").get_integer() == nullptr);
}

TEST_CASE("whitespace and trailing data") {
    CHECK(parse(" \t\r\n{ }\n").has_value());
    CHECK(error_of("") == ErrorCode::JsonSyntax);
    CHECK(error_of("   ") == ErrorCode::JsonSyntax);
    CHECK(error_of("{} {}") == ErrorCode::JsonSyntax);
    CHECK(error_of("{}x") == ErrorCode::JsonSyntax);
    CHECK(error_of("1 2") == ErrorCode::JsonSyntax);
    CHECK(error_of("\f{}") == ErrorCode::JsonSyntax);
}

TEST_CASE("syntax errors") {
    for (const char* text : {"{", "[", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "[1,]", "[,1]", "[1 2]", "{a:1}",
                             "{'a':1}", "{\"a\" 1}", "{\"a\":1 \"b\":2}", "]", "}", "[}", "{]", "nul", "tru", "truee",
                             "True", "NaN", "Infinity", "// c\n{}", "/* c */{}", "{\"a\":1}}", "\"abc", "[\"a\"",
                             "{\"a\":1"}) {
        CAPTURE(text);
        CHECK(error_of(text) == ErrorCode::JsonSyntax);
    }
}

TEST_CASE("nesting depth: the top-level container has depth 1, 32 is allowed") {
    CHECK(parse(nested('[', ']', 32)).has_value());
    CHECK(error_of(nested('[', ']', 33)) == ErrorCode::JsonNestingTooDeep);
    CHECK(parse(nested_objects(32)).has_value());
    CHECK(error_of(nested_objects(33)) == ErrorCode::JsonNestingTooDeep);

    CHECK(parse("[{\"a\":" + nested('[', ']', 30) + "}]").has_value());
    CHECK(error_of("[{\"a\":" + nested('[', ']', 31) + "}]") == ErrorCode::JsonNestingTooDeep);
}

TEST_CASE("very deep input fails cleanly without a deep recursion") {
    CHECK(error_of(std::string(100000, '[')) == ErrorCode::JsonNestingTooDeep);
    CHECK(error_of(std::string(100000, '{')) == ErrorCode::JsonSyntax);
}

TEST_CASE("the test helper utf8_of agrees with the compiler") {
    CHECK(utf8_of(0xE9) == U8("é"));
    CHECK(utf8_of(0x20AC) == U8("€"));
    CHECK(utf8_of(0x1F600) == U8("😀"));
    CHECK(utf8_of(0x41) == "A");
}

TEST_CASE("string escapes") {
    CHECK(*parse_ok(R"("a\"b\\c\/d\be\ff\ng\rh\ti")").get_string() == "a\"b\\c/d\be\ff\ng\rh\ti");
    CHECK(*parse_ok(quoted(esc("0041") + esc("00e9") + esc("20AC"))).get_string() ==
          utf8_of(0x41) + utf8_of(0xE9) + utf8_of(0x20AC));
    CHECK(*parse_ok(quoted(esc("D83D") + esc("DE00"))).get_string() == utf8_of(0x1F600));
    CHECK(*parse_ok(quoted(esc("d83d") + esc("de00"))).get_string() == utf8_of(0x1F600));
    CHECK(*parse_ok(quoted(esc("0000"))).get_string() == std::string(1, '\0'));
    CHECK(error_of(R"("\x")") == ErrorCode::JsonSyntax);
    CHECK(error_of(quoted(esc("12"))) == ErrorCode::JsonSyntax);
    CHECK(error_of(quoted(esc("12g4"))) == ErrorCode::JsonSyntax);
    CHECK(error_of("\"\\") == ErrorCode::JsonSyntax);
    CHECK(error_of("\"a\nb\"") == ErrorCode::JsonSyntax);
    CHECK(error_of(std::string("\"a") + '\x1f' + "b\"") == ErrorCode::JsonSyntax);
}

TEST_CASE("unpaired surrogates are rejected") {
    const std::vector<std::string> invalid = {
        esc("d800"),
        esc("dbff"),
        esc("dc00"),
        esc("dfff"),
        esc("d800") + "a",
        esc("d800") + esc("0041"),
        esc("d800") + esc("d800"),
        esc("dc00") + esc("d800"),
        esc("d800") + "\\n",
        "a" + esc("d83d"),
        esc("d83d") + esc("d83d") + esc("de00"),
    };
    for (const std::string& text : invalid) {
        CAPTURE(text);
        CHECK(error_of(quoted(text)) == ErrorCode::JsonUnpairedSurrogate);
    }
    CHECK(parse(quoted(esc("d800") + esc("dc00"))).has_value());
    CHECK(parse(quoted(esc("dbff") + esc("dfff"))).has_value());
    CHECK(parse(quoted(esc("DBFF") + esc("DFFF"))).has_value());
}

TEST_CASE("string length is counted in code points after unescaping") {
    const std::size_t max = limits::kJsonMaxStringCodePoints;

    CHECK(parse(quoted(std::string(max, 'a'))).has_value());
    CHECK(error_of(quoted(std::string(max + 1, 'a'))) == ErrorCode::JsonStringTooLong);

    CHECK(parse(quoted(repeat(utf8_of(0xE9), max))).has_value());
    CHECK(error_of(quoted(repeat(utf8_of(0xE9), max + 1))) == ErrorCode::JsonStringTooLong);

    CHECK(parse(quoted(repeat(utf8_of(0x1F600), max))).has_value());
    CHECK(error_of(quoted(repeat(utf8_of(0x1F600), max + 1))) == ErrorCode::JsonStringTooLong);

    CHECK(parse(quoted(repeat("\\u0041", max))).has_value());
    CHECK(error_of(quoted(repeat("\\u0041", max + 1))) == ErrorCode::JsonStringTooLong);

    CHECK(parse(quoted(repeat("\\ud83d\\ude00", max))).has_value());
    CHECK(error_of(quoted(repeat("\\ud83d\\ude00", max + 1))) == ErrorCode::JsonStringTooLong);

    CHECK(parse("[" + quoted(std::string(max, 'a')) + "," + quoted(std::string(max, 'b')) + "]").has_value());
}

TEST_CASE("the larger limit applies only to text_objects[].content and only when asked for") {
    const std::size_t normal = limits::kJsonMaxStringCodePoints;
    const std::size_t large = limits::kJsonMaxContentCodePoints;
    const JsonOptions long_content{true};

    const auto doc = [](const std::string& key_path_start, std::size_t length) {
        return key_path_start + quoted(std::string(length, 'x')) + "}]}";
    };
    const std::string text_objects = R"({"text_objects":[{"id":"t1","content":)";

    CHECK(parse(doc(text_objects, large), long_content).has_value());
    CHECK(error_of(doc(text_objects, large + 1), long_content) == ErrorCode::JsonStringTooLong);
    CHECK(parse(doc(text_objects, normal), long_content).has_value());
    CHECK(error_of(doc(text_objects, normal + 1)) == ErrorCode::JsonStringTooLong);

    const std::string other_key = R"({"text_objects":[{"id":)";
    CHECK(error_of(doc(other_key, normal + 1), long_content) == ErrorCode::JsonStringTooLong);

    const std::string other_array = R"({"assets":[{"content":)";
    CHECK(error_of(doc(other_array, normal + 1), long_content) == ErrorCode::JsonStringTooLong);

    const std::string too_shallow = R"({"content":)";
    CHECK(error_of(too_shallow + quoted(std::string(normal + 1, 'x')) + "}", long_content) ==
          ErrorCode::JsonStringTooLong);

    const std::string too_deep = R"({"text_objects":[{"x":{"content":)";
    CHECK(error_of(too_deep + quoted(std::string(normal + 1, 'x')) + "}}]}", long_content) ==
          ErrorCode::JsonStringTooLong);

    const std::string array_of_arrays = R"({"text_objects":[["content",)";
    CHECK(error_of(array_of_arrays + quoted(std::string(normal + 1, 'x')) + "]]}", long_content) ==
          ErrorCode::JsonStringTooLong);

    const std::string second_object = R"({"text_objects":[{"content":"a"},{"id":"b","content":)";
    CHECK(parse(second_object + quoted(std::string(large, 'x')) + "}]}", long_content).has_value());

    const std::string after_other_key = R"({"text_objects":[{"content":"a"}],"assets":[{"content":)";
    CHECK(error_of(after_other_key + quoted(std::string(normal + 1, 'x')) + "}]}", long_content) ==
          ErrorCode::JsonStringTooLong);
}

TEST_CASE("object keys are not subject to the string value limit") {
    const std::string long_key(limits::kJsonMaxStringCodePoints + 10, 'k');
    CHECK(parse("{" + quoted(long_key) + ":1}").has_value());
}

TEST_CASE("valid UTF-8 at the boundaries of each sequence length") {
    for (std::uint32_t cp : {0x80u, 0x7FFu, 0x800u, 0xD7FFu, 0xE000u, 0xFFFFu, 0x10000u, 0x10FFFFu}) {
        CAPTURE(cp);
        const std::string text = utf8_of(cp);
        const auto result = parse(quoted(text));
        REQUIRE(result.has_value());
        CHECK(*result.value().get_string() == text);
    }
}

TEST_CASE("invalid UTF-8 is rejected") {
    const std::vector<std::string> invalid = {
        "\x80",              // continuation byte alone
        "\xBF",
        "\xC0\x80",          // overlong
        "\xC1\xBF",
        "\xC2",              // truncated
        "\xC2\x41",          // bad continuation
        "\xE0\x80\x80",      // overlong
        "\xE0\x9F\xBF",
        "\xE1\x80",          // truncated
        "\xED\xA0\x80",      // encoded surrogate
        "\xED\xBF\xBF",
        "\xF0\x80\x80\x80",  // overlong
        "\xF0\x8F\xBF\xBF",
        "\xF4\x90\x80\x80",  // above U+10FFFF
        "\xF5\x80\x80\x80",
        "\xF8\x88\x80\x80\x80",
        "\xFF",
        "\xFE",
    };
    for (const std::string& bytes : invalid) {
        CAPTURE(bytes);
        CHECK(error_of(quoted(bytes)) == ErrorCode::JsonInvalidUtf8);
    }
}

TEST_CASE("non-ASCII bytes outside of strings are syntax errors") {
    CHECK(error_of("{\"a\":1}\xC3\xA9") == ErrorCode::JsonSyntax);
    CHECK(error_of("\xC3\xA9") == ErrorCode::JsonSyntax);
}

TEST_CASE("a byte order mark is an error") {
    CHECK(error_of("\xEF\xBB\xBF{}") == ErrorCode::JsonBom);
    CHECK(error_of("\xEF\xBB\xBF") == ErrorCode::JsonBom);
    CHECK(parse(" \xEF\xBB\xBF").has_value() == false);
}

TEST_CASE("duplicate keys are errors, also nested and after unescaping") {
    CHECK(error_of(R"({"a":1,"a":2})") == ErrorCode::JsonDuplicateKey);
    CHECK(error_of(R"({"a":1,"b":2,"a":3})") == ErrorCode::JsonDuplicateKey);
    CHECK(error_of(R"({"x":{"a":1,"a":2}})") == ErrorCode::JsonDuplicateKey);
    CHECK(error_of(R"([{"a":1},{"b":1,"b":1}])") == ErrorCode::JsonDuplicateKey);
    CHECK(error_of("{\"a\":1,\"" + esc("0061") + "\":2}") == ErrorCode::JsonDuplicateKey);
    CHECK(error_of(R"({"":1,"":2})") == ErrorCode::JsonDuplicateKey);

    CHECK(parse(R"({"a":1,"b":{"a":2},"c":[{"a":3}]})").has_value());
    CHECK(parse(R"({"a":1,"A":2})").has_value());
}

TEST_CASE("integers within +-(2^53 - 1)") {
    CHECK(*parse_ok("9007199254740991").get_integer() == 9007199254740991ll);
    CHECK(*parse_ok("-9007199254740991").get_integer() == -9007199254740991ll);
    CHECK(*parse_ok("[0]").get_array()->front().get_integer() == 0);
    CHECK(*parse_ok("1234567890").get_integer() == 1234567890ll);

    for (const char* text : {"9007199254740992", "-9007199254740992", "9223372036854775807", "9223372036854775808",
                             "99999999999999999999", "-99999999999999999999999999999999"}) {
        CAPTURE(text);
        CHECK(error_of(text) == ErrorCode::JsonIntegerOutOfRange);
    }
}

TEST_CASE("only plain integers are numbers") {
    for (const char* text : {"-0", "1.0", "5.0", "0.5", "-1.5", "1e2", "1E2", "1e+2", "1e-2", "0e0", "1.5e3", "0.0",
                             "-0.0", "-0e1", "10.", "1E400"}) {
        CAPTURE(text);
        CHECK(error_of(text) == ErrorCode::JsonNonIntegerNumber);
    }
    CHECK(error_of("[1.0]") == ErrorCode::JsonNonIntegerNumber);
    CHECK(error_of(R"({"a":2.5})") == ErrorCode::JsonNonIntegerNumber);
}

TEST_CASE("malformed numbers") {
    for (const char* text : {"-", "- 1", "+1", ".5", "01", "00", "-01", "1_000", "0x10", "--1", "1-"}) {
        CAPTURE(text);
        CHECK(error_of(text) == ErrorCode::JsonSyntax);
    }
}

TEST_CASE("the error offset points into the input") {
    const auto result = parse(R"({"a":1,"b":x})");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().offset == 11);

    CHECK(parse(R"({"a":1,"a":2})").error().offset > 0);
    CHECK(parse("\xEF\xBB\xBF{}").error().offset == 0);
}

TEST_CASE("a document shaped like meta.json") {
    const JsonValue v = parse_ok(R"({
        "width": 21000, "height": 29700,
        "background": {"type": "dotted", "color": "#FFFFFFFF", "foreground_color": "#000000FF", "spacing": 500},
        "text_objects": [], "assets": []
    })");
    CHECK(*v.find("width")->get_integer() == 21000);
    CHECK(*v.find("background")->find("type")->get_string() == "dotted");
    CHECK(v.find("text_objects")->get_array()->empty());
}
