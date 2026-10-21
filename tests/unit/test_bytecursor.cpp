#include "doctest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "onf/primitives/bytecursor.h"

using onf::ByteCursor;

namespace {

std::vector<std::uint8_t> le_bytes(std::uint64_t value, std::size_t width) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    return out;
}

}  // namespace

TEST_CASE("empty cursor") {
    ByteCursor cursor{{}};
    CHECK(cursor.position() == 0);
    CHECK(cursor.remaining() == 0);
    CHECK_FALSE(cursor.read_u8().has_value());
    CHECK(cursor.read_bytes(0).has_value());
    CHECK(cursor.skip(0).has_value());
}

TEST_CASE("read_u8 advances and fails at the end without moving") {
    const std::array<std::uint8_t, 2> data{7, 9};
    ByteCursor cursor{data};

    CHECK(cursor.read_u8().value() == 7);
    CHECK(cursor.position() == 1);
    CHECK(cursor.remaining() == 1);
    CHECK(cursor.read_u8().value() == 9);
    CHECK_FALSE(cursor.read_u8().has_value());
    CHECK(cursor.position() == 2);
}

TEST_CASE("little-endian example from spec 2.3.3") {
    const std::array<std::uint8_t, 4> data{0x78, 0x56, 0x34, 0x12};
    ByteCursor cursor{data};
    CHECK(cursor.read_u32().value() == 0x12345678u);
}

TEST_CASE("fixed-width reads round-trip") {
    const std::uint64_t values[] = {0, 1, 0xFF, 0x100, 0xFFFF, 0x10000, 0xFFFFFFFFull, 0x100000000ull,
                                    std::numeric_limits<std::uint64_t>::max()};
    for (std::uint64_t v : values) {
        CAPTURE(v);
        if (v <= 0xFF) {
            const auto bytes = le_bytes(v, 1);
            ByteCursor cursor{bytes};
            CHECK(cursor.read_u8().value() == v);
        }
        if (v <= 0xFFFF) {
            const auto bytes = le_bytes(v, 2);
            ByteCursor cursor{bytes};
            CHECK(cursor.read_u16().value() == v);
        }
        if (v <= 0xFFFFFFFFull) {
            const auto bytes = le_bytes(v, 4);
            ByteCursor cursor{bytes};
            CHECK(cursor.read_u32().value() == v);
        }
        const auto bytes = le_bytes(v, 8);
        ByteCursor cursor{bytes};
        CHECK(cursor.read_u64().value() == v);
        CHECK(cursor.remaining() == 0);
    }
}

TEST_CASE("one byte too few fails and leaves the position unchanged") {
    const std::vector<std::uint8_t> data(7, 0xAB);

    {
        ByteCursor cursor{std::span{data}.first(1)};
        CHECK_FALSE(cursor.read_u16().has_value());
        CHECK(cursor.position() == 0);
    }
    {
        ByteCursor cursor{std::span{data}.first(3)};
        CHECK_FALSE(cursor.read_u32().has_value());
        CHECK(cursor.position() == 0);
    }
    {
        ByteCursor cursor{data};
        CHECK_FALSE(cursor.read_u64().has_value());
        CHECK(cursor.position() == 0);
    }
}

TEST_CASE("read_bytes returns a window into the original buffer") {
    const std::array<std::uint8_t, 5> data{1, 2, 3, 4, 5};
    ByteCursor cursor{data};

    REQUIRE(cursor.skip(1).has_value());
    const auto window = cursor.read_bytes(3);
    REQUIRE(window.has_value());
    CHECK(window.value().data() == data.data() + 1);
    CHECK(window.value().size() == 3);
    CHECK(cursor.position() == 4);
    CHECK(cursor.remaining() == 1);
}

TEST_CASE("oversized counts fail without overflow") {
    const std::array<std::uint8_t, 4> data{1, 2, 3, 4};
    const std::size_t huge = std::numeric_limits<std::size_t>::max();

    ByteCursor cursor{data};
    REQUIRE(cursor.skip(1).has_value());

    CHECK_FALSE(cursor.read_bytes(4).has_value());
    CHECK_FALSE(cursor.read_bytes(huge).has_value());
    CHECK_FALSE(cursor.skip(huge).has_value());
    CHECK_FALSE(cursor.skip(huge - 1).has_value());
    CHECK(cursor.position() == 1);

    CHECK(cursor.read_bytes(3).has_value());
    CHECK(cursor.remaining() == 0);
}

TEST_CASE("mixed reads advance in order") {
    const std::array<std::uint8_t, 7> data{0x01, 0x02, 0x03, 0xAA, 0xBB, 0xCC, 0xDD};
    ByteCursor cursor{data};

    CHECK(cursor.read_u8().value() == 0x01);
    CHECK(cursor.read_u16().value() == 0x0302);
    CHECK(cursor.read_u32().value() == 0xDDCCBBAAu);
    CHECK(cursor.remaining() == 0);
}
