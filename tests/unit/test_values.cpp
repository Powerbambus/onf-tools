#include "doctest.h"

#include <array>
#include <cstdint>
#include <string>

#include "onf/read/values.h"

using namespace onf;

namespace {

std::string pad(unsigned value, std::size_t width) {
    std::string text = std::to_string(value);
    return std::string(width - text.size(), '0') + text;
}

std::string date(unsigned year, unsigned month, unsigned day, unsigned hour = 10, unsigned minute = 32,
                 unsigned second = 0, unsigned millisecond = 0) {
    return pad(year, 4) + "-" + pad(month, 2) + "-" + pad(day, 2) + "T" + pad(hour, 2) + ":" + pad(minute, 2) + ":" +
           pad(second, 2) + "." + pad(millisecond, 3) + "Z";
}

std::string hex_pair(unsigned value, bool upper) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    return {digits[value >> 4], digits[value & 0xF]};
}

}  // namespace

TEST_CASE("colors: #RRGGBBAA") {
    CHECK(parse_color("#000000FF") == Color{0, 0, 0, 255});
    CHECK(parse_color("#FFFFFF00") == Color{255, 255, 255, 0});
    CHECK(parse_color("#102030FF") == Color{0x10, 0x20, 0x30, 0xFF});
    CHECK(parse_color("#abcdef12") == Color{0xAB, 0xCD, 0xEF, 0x12});
    CHECK(parse_color("#ABCDEF12") == Color{0xAB, 0xCD, 0xEF, 0x12});
    CHECK(parse_color("#aBcDeF12") == Color{0xAB, 0xCD, 0xEF, 0x12});
}

TEST_CASE("colors: every value of every channel round-trips in both cases") {
    for (unsigned value = 0; value < 256; ++value) {
        CAPTURE(value);
        for (bool upper : {false, true}) {
            const std::string text = "#" + hex_pair(value, upper) + hex_pair(255 - value, upper) +
                                     hex_pair((value * 7) & 0xFF, upper) + hex_pair((value + 1) & 0xFF, upper);
            const auto color = parse_color(text);
            REQUIRE(color.has_value());
            CHECK(color->red == value);
            CHECK(color->green == 255 - value);
            CHECK(color->blue == ((value * 7) & 0xFF));
            CHECK(color->alpha == ((value + 1) & 0xFF));
        }
    }
}

TEST_CASE("colors: everything else is rejected") {
    for (const char* text : {"", "#", "000000FF", "#000000F", "#000000FFF", "#000000", "#00000G00", "#0000 0FF",
                             " #000000FF", "#000000FF ", "#000000FF\n", "0x000000FF", "##000000FF", "#-00000FF",
                             "#+00000FF", "#000000F\n"}) {
        CAPTURE(text);
        CHECK_FALSE(parse_color(text).has_value());
    }
    const std::string non_ascii = std::string("#00000") + "\xD9\xA1" + "FF";
    CHECK_FALSE(parse_color(non_ascii).has_value());
    CHECK_FALSE(parse_color(std::string("#000000F") + '\0').has_value());
}

TEST_CASE("versions: MAJOR.MINOR.PATCH") {
    CHECK(parse_version("0.2.0") == Version{0, 2, 0});
    CHECK(parse_version("0.0.0") == Version{0, 0, 0});
    CHECK(parse_version("1.0.0") == Version{1, 0, 0});
    CHECK(parse_version("10.20.30") == Version{10, 20, 30});
    CHECK(parse_version("4294967295.4294967295.4294967295") == Version{4294967295u, 4294967295u, 4294967295u});
}

TEST_CASE("versions: everything else is rejected") {
    for (const char* text : {"", "1", "1.2", "1.2.3.4", "1..3", ".1.2", "1.2.", "01.2.3", "1.02.3", "1.2.03", "00.0.0",
                             "0.2.0\n", " 0.2.0", "0.2.0 ", "0.2.0-draft", "v0.2.0", "-1.2.3", "+1.2.3", "1.2.-3",
                             "1,2,3", "1.2.3a", "a.b.c", "4294967296.0.0", "0.4294967296.0", "0.0.4294967296",
                             "99999999999999999999.0.0", "00000000001.0.0"}) {
        CAPTURE(text);
        CHECK_FALSE(parse_version(text).has_value());
    }
    const std::string non_ascii = std::string("\xD9\xA1") + ".2.3";
    CHECK_FALSE(parse_version(non_ascii).has_value());
    CHECK_FALSE(parse_version(std::string("1.2.3") + '\0').has_value());
}

TEST_CASE("datetime: the format from the specification") {
    const auto parsed = parse_utc_datetime("2026-06-14T10:32:00.000Z");
    REQUIRE(parsed.has_value());
    CHECK(*parsed == UtcDateTime{2026, 6, 14, 10, 32, 0, 0});

    CHECK(parse_utc_datetime("0000-01-01T00:00:00.000Z").has_value());
    CHECK(parse_utc_datetime("9999-12-31T23:59:59.999Z") == UtcDateTime{9999, 12, 31, 23, 59, 59, 999});
}

TEST_CASE("datetime: field ranges") {
    CHECK(parse_utc_datetime(date(2026, 1, 1, 23, 59, 59, 999)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2026, 1, 1, 24)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2026, 1, 1, 0, 60)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2026, 1, 1, 0, 0, 60)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2026, 0, 1)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2026, 13, 1)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2026, 1, 0)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2026, 1, 32)).has_value());
}

TEST_CASE("datetime: days per month in a leap and a non-leap year") {
    const std::array<unsigned, 12> regular = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (unsigned year : {2023u, 2024u}) {
        for (unsigned month = 1; month <= 12; ++month) {
            const unsigned days = regular[month - 1] + (month == 2 && year == 2024 ? 1 : 0);
            CAPTURE(year);
            CAPTURE(month);
            CHECK(parse_utc_datetime(date(year, month, days)).has_value());
            CHECK_FALSE(parse_utc_datetime(date(year, month, days + 1)).has_value());
        }
    }
}

TEST_CASE("datetime: February 29 is valid exactly in leap years") {
    for (unsigned year = 0; year <= 9999; ++year) {
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        CAPTURE(year);
        CHECK(parse_utc_datetime(date(year, 2, 29)).has_value() == leap);
    }
    CHECK_FALSE(parse_utc_datetime(date(1900, 2, 29)).has_value());
    CHECK(parse_utc_datetime(date(2000, 2, 29)).has_value());
    CHECK_FALSE(parse_utc_datetime(date(2100, 2, 29)).has_value());
}

TEST_CASE("datetime: anything but the exact format is rejected") {
    for (const char* text : {"", "2026-06-14", "2026-06-14T10:32:00Z", "2026-06-14T10:32:00.0Z",
                             "2026-06-14T10:32:00.00Z", "2026-06-14T10:32:00.0000Z", "2026-06-14T10:32:00.000",
                             "2026-06-14T10:32:00.000+00:00", "2026-06-14T10:32:00.000z", "2026-06-14t10:32:00.000Z",
                             "2026-06-14 10:32:00.000Z", "2026-06-14T10:32:00,000Z", "2026-06-14T10:32:00.000Z\n",
                             " 2026-06-14T10:32:00.000Z", "20260614T103200.000Z", "12026-06-14T10:32:00.000Z",
                             "-026-06-14T10:32:00.000Z", "2026-6-14T10:32:00.000Z", "2026-06-14T1:32:00.000Z",
                             "2026/06/14T10:32:00.000Z", "2026-06-14T10-32-00.000Z", "2026-06-14T10:32:0a.000Z",
                             "2026-06-14T10:32:00.00aZ"}) {
        CAPTURE(text);
        CHECK_FALSE(parse_utc_datetime(text).has_value());
    }
    CHECK_FALSE(parse_utc_datetime(std::string("2026-06-14T10:32:00.000") + '\0').has_value());
}
