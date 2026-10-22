#include "doctest.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "onf/primitives/utf8.h"

using namespace onf;

namespace {

std::span<const std::uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::string_view as_view(const char8_t* text) { return reinterpret_cast<const char*>(text); }

}  // namespace

TEST_CASE("utf8: valid sequences at the boundaries of each length (encoded by the compiler)") {
    struct Case {
        std::string_view text;
        std::size_t length;
    };
    const std::vector<Case> cases = {
        {"A", 1},
        {as_view(u8"\U00000080"), 2},
        {as_view(u8"\U000007FF"), 2},
        {as_view(u8"\U00000800"), 3},
        {as_view(u8"\U0000D7FF"), 3},
        {as_view(u8"\U0000E000"), 3},
        {as_view(u8"\U0000FFFF"), 3},
        {as_view(u8"\U00010000"), 4},
        {as_view(u8"\U0010FFFF"), 4},
    };
    for (const Case& c : cases) {
        CAPTURE(c.length);
        CHECK(c.text.size() == c.length);
        CHECK(utf8_sequence_length(bytes_of(c.text)) == c.length);
        CHECK(is_valid_utf8(bytes_of(c.text)));
    }
}

TEST_CASE("utf8: only the first sequence is measured") {
    const std::string text = std::string(as_view(u8"\U000020AC")) + "abc";
    CHECK(utf8_sequence_length(bytes_of(text)) == 3);
}

TEST_CASE("utf8: ill-formed input") {
    const std::vector<std::string> invalid = {
        "\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xC2", "\xC2\x41", "\xE0\x80\x80", "\xE0\x9F\xBF", "\xE1\x80",
        "\xE1\x80\x41", "\xED\xA0\x80", "\xED\xBF\xBF", "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF", "\xF4\x90\x80\x80",
        "\xF5\x80\x80\x80", "\xF8\x88\x80\x80\x80", "\xFE", "\xFF", "\xF0\x90\x80",
    };
    for (const std::string& text : invalid) {
        CAPTURE(text);
        CHECK(utf8_sequence_length(bytes_of(text)) == 0);
        CHECK_FALSE(is_valid_utf8(bytes_of(text)));
    }
    CHECK(utf8_sequence_length({}) == 0);
}

TEST_CASE("utf8: a whole string is valid only if every sequence is") {
    CHECK(is_valid_utf8({}));
    CHECK(is_valid_utf8(bytes_of("plain ascii")));
    CHECK(is_valid_utf8(bytes_of(as_view(u8"a\U000000E9b\U000020ACc\U0001F600"))));
    CHECK_FALSE(is_valid_utf8(bytes_of(std::string("abc") + "\xC3")));
    CHECK_FALSE(is_valid_utf8(bytes_of(std::string("\xC3") + "abc")));
    CHECK(is_valid_utf8(bytes_of(std::string("a") + '\0' + "b")));
}
