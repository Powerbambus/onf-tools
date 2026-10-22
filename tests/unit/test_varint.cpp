#include "doctest.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "onf/errors.h"
#include "onf/limits.h"
#include "onf/primitives/bytecursor.h"
#include "onf/primitives/varint.h"
#include "support/varint_encoder.h"
#include "support/vectors.h"

using namespace onf;
using namespace onf::limits;
using namespace onf::test;

namespace {

constexpr std::uint64_t kPow2_33 = 1ull << 33;
constexpr std::uint64_t kPow2_32 = 1ull << 32;

ErrorCode decode_error(const std::vector<std::uint8_t>& bytes, std::uint64_t max_value) {
    ByteCursor cursor{bytes};
    const auto result = decode_varint(cursor, max_value);
    REQUIRE_FALSE(result.has_value());
    CHECK(cursor.position() == 0);
    return result.error();
}

}  // namespace

TEST_CASE("Appendix A: zigzag vectors") {
    for (const Row& row : load_vectors("zigzag.tsv")) {
        const std::int64_t signed_value = std::stoll(row.at(0));
        const std::uint64_t zigzag = std::stoull(row.at(1));
        const std::vector<std::uint8_t> bytes = parse_hex_bytes(row.at(2));
        CAPTURE(signed_value);

        ByteCursor cursor{bytes};
        const auto decoded = decode_varint(cursor, kVarintMaxValue);
        REQUIRE(decoded.has_value());
        CHECK(decoded.value() == zigzag);
        CHECK(cursor.remaining() == 0);
        CHECK(zigzag_decode(decoded.value()) == signed_value);

        CHECK(zigzag_encode(signed_value) == zigzag);
        CHECK(encode_varint(zigzag) == bytes);
    }
}

TEST_CASE("Appendix A: plain varint vectors") {
    for (const Row& row : load_vectors("varint_plain.tsv")) {
        const std::uint64_t value = std::stoull(row.at(0));
        const std::vector<std::uint8_t> bytes = parse_hex_bytes(row.at(1));
        CAPTURE(value);

        ByteCursor cursor{bytes};
        const auto decoded = decode_varint(cursor, kTimestampMaxValue);
        REQUIRE(decoded.has_value());
        CHECK(decoded.value() == value);
        CHECK(cursor.remaining() == 0);

        CHECK(encode_varint(value) == bytes);
    }
}

TEST_CASE("Appendix A: malformed varints are rejected with the right error") {
    for (const Row& row : load_vectors("varint_malformed.tsv")) {
        const std::vector<std::uint8_t> bytes = parse_hex_bytes(row.at(0));
        const std::uint64_t max_value = row.at(1) == "timestamp" ? kTimestampMaxValue : kVarintMaxValue;
        CAPTURE(row.at(0));
        CAPTURE(row.at(1));

        const ErrorCode code = decode_error(bytes, max_value);
        CHECK(error_name(code) == row.at(2));
    }
}

TEST_CASE("round trip around every byte-length boundary") {
    for (unsigned k = 1; k <= 4; ++k) {
        const std::uint64_t boundary = 1ull << (7 * k);
        for (std::uint64_t value : {boundary - 1, boundary, boundary + 1}) {
            CAPTURE(value);
            const auto bytes = encode_varint(value);
            CHECK(bytes.size() == (value < boundary ? k : k + 1));

            ByteCursor cursor{bytes};
            const auto decoded = decode_varint(cursor, kVarintMaxValue);
            REQUIRE(decoded.has_value());
            CHECK(decoded.value() == value);
            CHECK(cursor.remaining() == 0);
        }
    }
}

TEST_CASE("range of the fifth byte") {
    {
        const auto bytes = encode_varint(kVarintMaxValue);
        CHECK(bytes.size() == 5);
        ByteCursor cursor{bytes};
        CHECK(decode_varint(cursor, kVarintMaxValue).value() == kVarintMaxValue);
    }
    CHECK(decode_error(encode_varint(kPow2_33), kVarintMaxValue) == ErrorCode::VarintOutOfRange);

    {
        const auto bytes = encode_varint(kTimestampMaxValue);
        ByteCursor cursor{bytes};
        CHECK(decode_varint(cursor, kTimestampMaxValue).value() == kTimestampMaxValue);
    }
    CHECK(decode_error(encode_varint(kPow2_32), kTimestampMaxValue) == ErrorCode::VarintOutOfRange);

    const auto bytes = encode_varint(kPow2_32);
    ByteCursor cursor{bytes};
    CHECK(decode_varint(cursor, kVarintMaxValue).value() == kPow2_32);
}

TEST_CASE("padding any canonical varint makes it non-canonical") {
    for (std::uint64_t value : {0ull, 1ull, 127ull, 128ull, 16383ull, 16384ull, 268435455ull}) {
        CAPTURE(value);
        auto bytes = encode_varint(value);
        bytes.back() |= 0x80;
        bytes.push_back(0x00);
        CHECK(decode_error(bytes, kVarintMaxValue) == ErrorCode::VarintNotCanonical);
    }
}

TEST_CASE("a varint cut off at any point is truncated") {
    const auto full = encode_varint(kVarintMaxValue);
    REQUIRE(full.size() == 5);

    CHECK(decode_error({}, kVarintMaxValue) == ErrorCode::VarintTruncated);
    for (std::size_t length = 1; length < full.size(); ++length) {
        CAPTURE(length);
        const std::vector<std::uint8_t> prefix(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length));
        CHECK(decode_error(prefix, kVarintMaxValue) == ErrorCode::VarintTruncated);
    }
}

TEST_CASE("consecutive varints are decoded in order") {
    std::vector<std::uint8_t> bytes;
    const std::uint64_t values[] = {5, 300, 0, kVarintMaxValue, 1};
    for (std::uint64_t v : values) {
        const auto encoded = encode_varint(v);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }

    ByteCursor cursor{bytes};
    for (std::uint64_t v : values) {
        const auto decoded = decode_varint(cursor, kVarintMaxValue);
        REQUIRE(decoded.has_value());
        CHECK(decoded.value() == v);
    }
    CHECK(cursor.remaining() == 0);
}

TEST_CASE("a failed decode leaves the cursor where it was") {
    const std::vector<std::uint8_t> bytes{0x01, 0x80, 0x00};
    ByteCursor cursor{bytes};
    REQUIRE(decode_varint(cursor, kVarintMaxValue).has_value());
    REQUIRE(cursor.position() == 1);

    const auto failed = decode_varint(cursor, kVarintMaxValue);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error() == ErrorCode::VarintNotCanonical);
    CHECK(cursor.position() == 1);
}

TEST_CASE("zigzag round trip without overflow") {
    const std::int64_t values[] = {0, 1, -1, 2147483647, -2147483647 - 1, 4294967295, -4294967295,
                                   std::numeric_limits<std::int64_t>::max(),
                                   std::numeric_limits<std::int64_t>::min()};
    for (std::int64_t v : values) {
        CAPTURE(v);
        CHECK(zigzag_decode(zigzag_encode(v)) == v);
    }
    CHECK(zigzag_encode(4294967295) == 8589934590ull);
    CHECK(zigzag_encode(-4294967295) == 8589934589ull);
}
