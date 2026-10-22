#pragma once

#include <cstdint>

namespace onf {

using Coord = std::int32_t;      // 1/100 mm, spec 5.1
using Timestamp = std::uint32_t; // ms since the start of a stroke, spec 5.3
using MsTime = std::uint64_t;    // ms since the Unix epoch, spec 5.5

struct Color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;  // straight alpha, 255 is opaque (spec 5.2)

    friend bool operator==(const Color&, const Color&) = default;
};

static_assert(sizeof(Color) == 4);

// Calendar date and time in UTC with millisecond precision, as used by the manifest (spec 6.1.2).
struct UtcDateTime {
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;
    std::uint16_t millisecond = 0;

    friend bool operator==(const UtcDateTime&, const UtcDateTime&) = default;
};

// Stroke flags, spec 7.4.3. Undefined bits are kept in raw() and ignored by the accessors.
class StrokeFlags {
public:
    constexpr StrokeFlags() noexcept = default;
    constexpr explicit StrokeFlags(std::uint16_t raw) noexcept : raw_(raw) {}

    constexpr std::uint16_t raw() const noexcept { return raw_; }
    constexpr bool has_tilt() const noexcept { return (raw_ & kHasTilt) != 0; }
    constexpr bool has_azimuth() const noexcept { return (raw_ & kHasAzimuth) != 0; }
    constexpr bool no_pressure() const noexcept { return (raw_ & kNoPressure) != 0; }

    friend constexpr bool operator==(StrokeFlags, StrokeFlags) noexcept = default;

private:
    static constexpr std::uint16_t kHasTilt = 1u << 0;
    static constexpr std::uint16_t kHasAzimuth = 1u << 1;
    static constexpr std::uint16_t kNoPressure = 1u << 2;

    std::uint16_t raw_ = 0;
};

}  // namespace onf
