#include "onf/read/values.h"

#include <array>
#include <cstdint>

namespace onf {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_value(char c) {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool is_leap_year(unsigned year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

unsigned days_in_month(unsigned year, unsigned month) {
    constexpr std::array<unsigned, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap_year(year) ? 29 : days[month - 1];
}

// Reads exactly `count` ASCII digits starting at `pos`.
std::optional<unsigned> digits(std::string_view text, std::size_t pos, std::size_t count) {
    unsigned value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!is_digit(text[pos + i])) return std::nullopt;
        value = value * 10 + static_cast<unsigned>(text[pos + i] - '0');
    }
    return value;
}

std::optional<std::uint32_t> version_component(std::string_view part) {
    if (part.empty() || part.size() > 10) return std::nullopt;
    if (part.size() > 1 && part[0] == '0') return std::nullopt;

    std::uint64_t value = 0;
    for (char c : part) {
        if (!is_digit(c)) return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value > UINT32_MAX) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

}  // namespace

std::optional<Color> parse_color(std::string_view text) {
    if (text.size() != 9 || text[0] != '#') return std::nullopt;

    std::array<std::uint8_t, 4> channels{};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const int high = hex_value(text[1 + 2 * i]);
        const int low = hex_value(text[2 + 2 * i]);
        if (high < 0 || low < 0) return std::nullopt;
        channels[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return Color{channels[0], channels[1], channels[2], channels[3]};
}

std::optional<Version> parse_version(std::string_view text) {
    const std::size_t first_dot = text.find('.');
    if (first_dot == std::string_view::npos) return std::nullopt;
    const std::size_t second_dot = text.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) return std::nullopt;

    const auto major = version_component(text.substr(0, first_dot));
    const auto minor = version_component(text.substr(first_dot + 1, second_dot - first_dot - 1));
    const auto patch = version_component(text.substr(second_dot + 1));
    if (!major || !minor || !patch) return std::nullopt;
    return Version{*major, *minor, *patch};
}

std::optional<UtcDateTime> parse_utc_datetime(std::string_view text) {
    if (text.size() != 24) return std::nullopt;
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != '.' ||
        text[23] != 'Z') {
        return std::nullopt;
    }

    const auto year = digits(text, 0, 4);
    const auto month = digits(text, 5, 2);
    const auto day = digits(text, 8, 2);
    const auto hour = digits(text, 11, 2);
    const auto minute = digits(text, 14, 2);
    const auto second = digits(text, 17, 2);
    const auto millisecond = digits(text, 20, 3);
    if (!year || !month || !day || !hour || !minute || !second || !millisecond) return std::nullopt;

    if (*month < 1 || *month > 12) return std::nullopt;
    if (*day < 1 || *day > days_in_month(*year, *month)) return std::nullopt;
    if (*hour > 23 || *minute > 59 || *second > 59) return std::nullopt;

    return UtcDateTime{static_cast<std::uint16_t>(*year),   static_cast<std::uint8_t>(*month),
                       static_cast<std::uint8_t>(*day),     static_cast<std::uint8_t>(*hour),
                       static_cast<std::uint8_t>(*minute),  static_cast<std::uint8_t>(*second),
                       static_cast<std::uint16_t>(*millisecond)};
}

}  // namespace onf
