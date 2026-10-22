#include "doctest.h"

#include <cstdint>
#include <limits>
#include <type_traits>

#include "onf/model/types.h"

using namespace onf;

TEST_CASE("type widths follow the specification") {
    static_assert(std::is_same_v<Coord, std::int32_t>);
    static_assert(std::is_same_v<Timestamp, std::uint32_t>);
    static_assert(std::is_same_v<MsTime, std::uint64_t>);
    static_assert(std::is_trivially_copyable_v<Color>);
    static_assert(std::is_trivially_copyable_v<StrokeFlags>);
    CHECK(sizeof(Color) == 4);
    CHECK(sizeof(StrokeFlags) == 2);
}

TEST_CASE("Color keeps its channels and compares by value") {
    const Color a{1, 2, 3, 4};
    CHECK(a.red == 1);
    CHECK(a.green == 2);
    CHECK(a.blue == 3);
    CHECK(a.alpha == 4);
    CHECK(a == Color{1, 2, 3, 4});
    CHECK_FALSE(a == Color{1, 2, 3, 5});
    CHECK(Color{} == Color{0, 0, 0, 0});
}

TEST_CASE("StrokeFlags: each defined bit controls exactly one accessor") {
    const StrokeFlags none;
    CHECK_FALSE(none.has_tilt());
    CHECK_FALSE(none.has_azimuth());
    CHECK_FALSE(none.no_pressure());

    const StrokeFlags tilt{1u << 0};
    CHECK(tilt.has_tilt());
    CHECK_FALSE(tilt.has_azimuth());
    CHECK_FALSE(tilt.no_pressure());

    const StrokeFlags azimuth{1u << 1};
    CHECK_FALSE(azimuth.has_tilt());
    CHECK(azimuth.has_azimuth());
    CHECK_FALSE(azimuth.no_pressure());

    const StrokeFlags pressure{1u << 2};
    CHECK_FALSE(pressure.has_tilt());
    CHECK_FALSE(pressure.has_azimuth());
    CHECK(pressure.no_pressure());
}

TEST_CASE("StrokeFlags: undefined bits are ignored but preserved") {
    for (unsigned bit = 3; bit < 16; ++bit) {
        CAPTURE(bit);
        const StrokeFlags flags{static_cast<std::uint16_t>(1u << bit)};
        CHECK_FALSE(flags.has_tilt());
        CHECK_FALSE(flags.has_azimuth());
        CHECK_FALSE(flags.no_pressure());
        CHECK(flags.raw() == (1u << bit));
    }

    const StrokeFlags all{std::numeric_limits<std::uint16_t>::max()};
    CHECK(all.has_tilt());
    CHECK(all.has_azimuth());
    CHECK(all.no_pressure());
    CHECK(all.raw() == std::numeric_limits<std::uint16_t>::max());
}
