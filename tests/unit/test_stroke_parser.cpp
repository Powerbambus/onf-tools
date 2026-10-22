#include "doctest.h"

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "onf/errors.h"
#include "onf/limits.h"
#include "onf/read/stroke_parser.h"
#include "support/strokes_builder.h"

using namespace onf;
using namespace onf::test;

namespace {

constexpr std::int64_t kInt32Min = std::numeric_limits<std::int32_t>::min();
constexpr std::int64_t kInt32Max = std::numeric_limits<std::int32_t>::max();
constexpr std::uint64_t kTimestampMax = limits::kTimestampMaxValue;

Stroke parse_ok(const Bytes& body) {
    auto result = parse_stroke(body);
    REQUIRE_MESSAGE(result.has_value(), "unexpected error: " << result.error().message);
    return std::move(result).value();
}

StrokeParseError parse_failure(const Bytes& body) {
    const auto result = parse_stroke(body);
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

Bytes body_of(const StrokeHeaderIn& header, const std::vector<SampleIn>& samples) { return stroke_body(header, samples); }

Bytes body_with_count(std::uint32_t count, std::uint16_t flags, const Bytes& sample_bytes) {
    StrokeHeaderIn header;
    header.sample_count = count;
    header.flags = flags;
    return concat({stroke_header(header), sample_bytes});
}

}  // namespace

TEST_CASE("stroke header fields") {
    StrokeHeaderIn header;
    header.stroke_id = 0xA1B2C3D4u;
    header.created_at = 0x0102030405060708ull;
    header.color = {10, 20, 30, 40};
    header.tool = 3;
    header.base_width = 515;
    header.flags = 0;

    const Stroke stroke = parse_ok(body_of(header, {SampleIn{}}));
    CHECK(stroke.stroke_id == 0xA1B2C3D4u);
    CHECK(stroke.created_at == 0x0102030405060708ull);
    CHECK(stroke.color == Color{10, 20, 30, 40});
    CHECK(stroke.tool_type_raw == 3);
    CHECK(stroke.tool == ToolType::Highlighter);
    CHECK(stroke.base_width == 515);
    CHECK(stroke.samples.size() == 1);
}

TEST_CASE("unknown tool types fall back to the ballpoint pen and keep the raw value") {
    for (std::uint16_t tool : std::vector<std::uint16_t>{5, 6, 255, 0xFFFF}) {
        CAPTURE(tool);
        StrokeHeaderIn header;
        header.tool = tool;
        const Stroke stroke = parse_ok(body_of(header, {SampleIn{}}));
        CHECK(stroke.tool == ToolType::Ballpoint);
        CHECK(stroke.tool_type_raw == tool);
    }
    for (std::uint16_t tool = 0; tool <= 4; ++tool) {
        StrokeHeaderIn header;
        header.tool = tool;
        CHECK(static_cast<std::uint16_t>(parse_ok(body_of(header, {SampleIn{}})).tool) == tool);
    }
}

TEST_CASE("undefined flag bits are ignored but kept") {
    StrokeHeaderIn header;
    header.flags = 0xFFF8;
    const Stroke stroke = parse_ok(body_of(header, {SampleIn{}}));
    CHECK(stroke.flags.raw() == 0xFFF8);
    CHECK_FALSE(stroke.flags.has_tilt());
    CHECK_FALSE(stroke.flags.has_azimuth());
    CHECK(stroke.samples.tilt_x.empty());
    CHECK(stroke.samples.azimuth.empty());
}

TEST_CASE("example from spec 7.5.2: stored varint values and decoded samples") {
    CHECK(zigzag_encode(10000) == 20000);
    CHECK(zigzag_encode(5000) == 10000);
    CHECK(zigzag_encode(200) == 400);
    CHECK(zigzag_encode(4) == 8);
    CHECK(zigzag_encode(3) == 6);
    CHECK(zigzag_encode(2) == 4);
    CHECK(zigzag_encode(5) == 10);
    CHECK(zigzag_encode(-2) == 3);

    const std::vector<SampleIn> in = {{10000, 5000, 200, 0}, {10004, 5003, 202, 8}, {10009, 5007, 200, 16}};
    const Stroke stroke = parse_ok(body_of(StrokeHeaderIn{}, in));

    CHECK(stroke.samples.x == std::vector<Coord>{10000, 10004, 10009});
    CHECK(stroke.samples.y == std::vector<Coord>{5000, 5003, 5007});
    CHECK(stroke.samples.pressure == std::vector<std::uint8_t>{200, 202, 200});
    CHECK(stroke.samples.timestamp == std::vector<Timestamp>{0, 8, 16});
}

TEST_CASE("the first sample is a zigzag delta against zero, so it may be negative") {
    const Stroke stroke = parse_ok(body_of(StrokeHeaderIn{}, {SampleIn{-500, -1, 0, 0}, SampleIn{-499, 7, 10, 3}}));
    CHECK(stroke.samples.x == std::vector<Coord>{-500, -499});
    CHECK(stroke.samples.y == std::vector<Coord>{-1, 7});
    CHECK(stroke.samples.pressure == std::vector<std::uint8_t>{0, 10});
}

TEST_CASE("the largest possible delta between two i32 values") {
    const Stroke stroke = parse_ok(body_of(StrokeHeaderIn{}, {SampleIn{kInt32Min, kInt32Max}, SampleIn{kInt32Max, kInt32Min}}));
    CHECK(stroke.samples.x == std::vector<Coord>{std::numeric_limits<Coord>::min(), std::numeric_limits<Coord>::max()});
    CHECK(stroke.samples.y == std::vector<Coord>{std::numeric_limits<Coord>::max(), std::numeric_limits<Coord>::min()});
}

TEST_CASE("tilt and azimuth fields") {
    StrokeHeaderIn header;
    header.flags = kFlagHasTilt | kFlagHasAzimuth;
    SampleIn a;
    a.tilt_x = -90;
    a.tilt_y = 90;
    a.azimuth = 35999;
    SampleIn b;
    b.tilt_x = 90;
    b.tilt_y = -90;
    b.azimuth = 0;

    const Stroke stroke = parse_ok(body_of(header, {a, b}));
    CHECK(stroke.samples.tilt_x == std::vector<std::int8_t>{-90, 90});
    CHECK(stroke.samples.tilt_y == std::vector<std::int8_t>{90, -90});
    CHECK(stroke.samples.azimuth == std::vector<std::uint16_t>{35999, 0});
}

TEST_CASE("only tilt, only azimuth") {
    StrokeHeaderIn tilt_only;
    tilt_only.flags = kFlagHasTilt;
    SampleIn s;
    s.tilt_x = 5;
    s.tilt_y = -6;
    const Stroke a = parse_ok(body_of(tilt_only, {s}));
    CHECK(a.samples.tilt_x == std::vector<std::int8_t>{5});
    CHECK(a.samples.azimuth.empty());

    StrokeHeaderIn azimuth_only;
    azimuth_only.flags = kFlagHasAzimuth;
    s.azimuth = 1234;
    const Stroke b = parse_ok(body_of(azimuth_only, {s}));
    CHECK(b.samples.tilt_x.empty());
    CHECK(b.samples.azimuth == std::vector<std::uint16_t>{1234});
}

TEST_CASE("NO_PRESSURE does not change how pressure is read") {
    StrokeHeaderIn header;
    header.flags = 1u << 2;
    const Stroke stroke = parse_ok(body_of(header, {SampleIn{0, 0, 255}, SampleIn{1, 1, 100}}));
    CHECK(stroke.flags.no_pressure());
    CHECK(stroke.samples.pressure == std::vector<std::uint8_t>{255, 100});
}

TEST_CASE("pressure range 0 to 255") {
    CHECK(parse_ok(body_of(StrokeHeaderIn{}, {SampleIn{0, 0, 0}})).samples.pressure[0] == 0);
    CHECK(parse_ok(body_of(StrokeHeaderIn{}, {SampleIn{0, 0, 255}})).samples.pressure[0] == 255);
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{0, 0, 256}})).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{0, 0, -1}})).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{0, 0, 255}, SampleIn{0, 0, 256}})).code ==
          ErrorCode::SampleValueOutOfRange);
}

TEST_CASE("x and y must stay inside i32 after every sample") {
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{kInt32Max + 1}})).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{kInt32Min - 1}})).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{0, kInt32Max + 1}})).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{0, kInt32Min - 1}})).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(body_of(StrokeHeaderIn{}, {SampleIn{kInt32Max}, SampleIn{kInt32Max + 1}})).code ==
          ErrorCode::SampleValueOutOfRange);
}

TEST_CASE("tilt must be within 90 degrees, azimuth within 0 to 35999") {
    StrokeHeaderIn header;
    header.flags = kFlagHasTilt | kFlagHasAzimuth;

    auto with = [&](auto setter) {
        SampleIn s;
        setter(s);
        return body_of(header, {s});
    };

    CHECK(parse_failure(with([](SampleIn& s) { s.tilt_x = 91; })).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(with([](SampleIn& s) { s.tilt_x = -91; })).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(with([](SampleIn& s) { s.tilt_y = 91; })).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(with([](SampleIn& s) { s.tilt_y = -91; })).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(with([](SampleIn& s) { s.azimuth = 36000; })).code == ErrorCode::SampleValueOutOfRange);
    CHECK(parse_failure(with([](SampleIn& s) { s.azimuth = -1; })).code == ErrorCode::SampleValueOutOfRange);
}

TEST_CASE("timestamps accumulate and may not exceed 2^32 - 1") {
    SampleIn first;
    first.timestamp = kTimestampMax;
    CHECK(parse_ok(body_of(StrokeHeaderIn{}, {first})).samples.timestamp[0] == kTimestampMax);

    SampleIn a;
    a.timestamp = kTimestampMax - 1;
    SampleIn b;
    b.timestamp = kTimestampMax;
    CHECK(parse_ok(body_of(StrokeHeaderIn{}, {a, b})).samples.timestamp.back() == kTimestampMax);

    StrokeHeaderIn header;
    header.sample_count = 2;
    const Bytes two_samples = concat({stroke_header(header),
                                      zz(0), zz(0), zz(0), plain(kTimestampMax),
                                      zz(0), zz(0), zz(0), plain(1)});
    const StrokeParseError error = parse_failure(two_samples);
    CHECK(error.code == ErrorCode::SampleValueOutOfRange);
}

TEST_CASE("a single varint above the timestamp range is rejected") {
    StrokeHeaderIn header;
    header.sample_count = 1;
    const Bytes body = concat({stroke_header(header), zz(0), zz(0), zz(0), plain(1ull << 32)});
    CHECK(parse_failure(body).code == ErrorCode::VarintOutOfRange);
}

TEST_CASE("header errors") {
    CHECK(parse_failure(filler(25)).code == ErrorCode::StrokeHeaderTooShort);
    CHECK(parse_failure({}).code == ErrorCode::StrokeHeaderTooShort);

    StrokeHeaderIn header;
    header.sample_count = 0;
    const StrokeParseError zero_count = parse_failure(stroke_header(header));
    CHECK(zero_count.code == ErrorCode::StrokeSampleCountZero);
    CHECK(zero_count.offset == 22);

    header.sample_count = 1;
    header.base_width = 0;
    const StrokeParseError zero_width = parse_failure(concat({stroke_header(header), zz(0), zz(0), zz(255), plain(0)}));
    CHECK(zero_width.code == ErrorCode::StrokeBaseWidthZero);
    CHECK(zero_width.offset == 18);
}

TEST_CASE("sample_count larger than the body can hold fails before any allocation") {
    const Bytes huge = body_with_count(std::numeric_limits<std::uint32_t>::max(), 0, filler(16, 0));
    CHECK(parse_failure(huge).code == ErrorCode::StrokeSamplesTruncated);

    const Bytes four_bytes_each = filler(4 * 1000, 0);
    CHECK(parse_ok(body_with_count(1000, 0, four_bytes_each)).samples.size() == 1000);
    CHECK(parse_failure(body_with_count(1001, 0, four_bytes_each)).code == ErrorCode::StrokeSamplesTruncated);

    CHECK(parse_failure(body_with_count(1000, kFlagHasTilt, four_bytes_each)).code == ErrorCode::StrokeSamplesTruncated);
    CHECK(parse_failure(body_with_count(1000, kFlagHasAzimuth, four_bytes_each)).code ==
          ErrorCode::StrokeSamplesTruncated);
}

TEST_CASE("a sample block cut off by one byte is truncated") {
    const std::vector<SampleIn> samples = {{100, 200, 50, 5}, {300, 400, 60, 9}};
    const Bytes full = body_of(StrokeHeaderIn{}, samples);
    REQUIRE(parse_stroke(full).has_value());

    for (std::size_t cut = 1; cut <= 8; ++cut) {
        CAPTURE(cut);
        const Bytes shortened(full.begin(), full.end() - static_cast<std::ptrdiff_t>(cut));
        const auto result = parse_stroke(shortened);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::StrokeSamplesTruncated);
    }
}

TEST_CASE("bytes after the last sample are ignored") {
    Bytes body = body_of(StrokeHeaderIn{}, {SampleIn{1, 2, 3, 4}});
    const Stroke plain_stroke = parse_ok(body);
    append(body, filler(20, 0xAB));
    const Stroke padded = parse_ok(body);
    CHECK(padded.samples.x == plain_stroke.samples.x);
    CHECK(padded.samples.timestamp == plain_stroke.samples.timestamp);
}

TEST_CASE("varint errors inside the sample block keep their code") {
    StrokeHeaderIn header;
    header.sample_count = 1;
    const Bytes not_canonical = concat({stroke_header(header), Bytes{0x80, 0x00}, zz(0), zz(0), plain(0)});
    const StrokeParseError error = parse_failure(not_canonical);
    CHECK(error.code == ErrorCode::VarintNotCanonical);
    CHECK(error.offset == 26);

    const Bytes too_long = concat({stroke_header(header), Bytes{0x80, 0x80, 0x80, 0x80, 0x80}, zz(0), zz(0), plain(0)});
    CHECK(parse_failure(too_long).code == ErrorCode::VarintTooLong);
}

TEST_CASE("random strokes round-trip through the encoder and the parser") {
    std::mt19937_64 rng(12345);
    auto uniform = [&](std::int64_t lo, std::int64_t hi) {
        return std::uniform_int_distribution<std::int64_t>(lo, hi)(rng);
    };

    for (int round = 0; round < 200; ++round) {
        StrokeHeaderIn header;
        header.flags = static_cast<std::uint16_t>(uniform(0, 7));
        std::vector<SampleIn> samples;
        std::uint64_t t = 0;
        const int count = static_cast<int>(uniform(1, 60));
        for (int i = 0; i < count; ++i) {
            SampleIn s;
            s.x = uniform(kInt32Min, kInt32Max);
            s.y = uniform(kInt32Min, kInt32Max);
            s.pressure = uniform(0, 255);
            t += static_cast<std::uint64_t>(uniform(0, 100000));
            s.timestamp = t;
            s.tilt_x = uniform(-90, 90);
            s.tilt_y = uniform(-90, 90);
            s.azimuth = uniform(0, 35999);
            samples.push_back(s);
        }

        CAPTURE(round);
        const Stroke stroke = parse_ok(body_of(header, samples));
        REQUIRE(stroke.samples.size() == samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            CHECK(stroke.samples.x[i] == samples[i].x);
            CHECK(stroke.samples.y[i] == samples[i].y);
            CHECK(stroke.samples.pressure[i] == samples[i].pressure);
            CHECK(stroke.samples.timestamp[i] == samples[i].timestamp);
            if (header.flags & kFlagHasTilt) {
                CHECK(stroke.samples.tilt_x[i] == samples[i].tilt_x);
                CHECK(stroke.samples.tilt_y[i] == samples[i].tilt_y);
            }
            if (header.flags & kFlagHasAzimuth) CHECK(stroke.samples.azimuth[i] == samples[i].azimuth);
        }
    }
}

namespace {

Bytes simple_stroke_chunk(std::uint32_t id, std::int64_t x = 0) {
    StrokeHeaderIn header;
    header.stroke_id = id;
    return chunk(0x0001, body_of(header, {SampleIn{x, 0, 255, 0}}));
}

Result<std::vector<Stroke>, PageError> parse_page(const Bytes& file, DiagnosticsSink* sink = nullptr) {
    return parse_strokes(file, "p00003", sink);
}

}  // namespace

TEST_CASE("parse_strokes returns the strokes in file order") {
    const Bytes file = concat({file_header(), simple_stroke_chunk(7, 10), chunk(0x0042, filler(5)),
                               simple_stroke_chunk(3, 20), chunk(0x0002)});
    const auto result = parse_page(file);
    REQUIRE(result.has_value());
    REQUIRE(result.value().size() == 2);
    CHECK(result.value()[0].stroke_id == 7);
    CHECK(result.value()[0].samples.x[0] == 10);
    CHECK(result.value()[1].stroke_id == 3);
}

TEST_CASE("a duplicate stroke_id makes the whole page malformed") {
    const Bytes first = simple_stroke_chunk(5);
    const Bytes file = concat({file_header(), first, simple_stroke_chunk(6), simple_stroke_chunk(5)});
    const auto result = parse_page(file);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::StrokeIdDuplicate);
    CHECK(result.error().page_id == "p00003");
    CHECK(result.error().offset == 8 + 2 * first.size() + 6);
}

TEST_CASE("one bad stroke gives an error and no partial result") {
    StrokeHeaderIn bad;
    bad.stroke_id = 9;
    bad.sample_count = 5;
    const Bytes bad_chunk = chunk(0x0001, concat({stroke_header(bad), zz(0)}));
    const Bytes file = concat({file_header(), simple_stroke_chunk(1), bad_chunk, simple_stroke_chunk(2)});

    const auto result = parse_page(file);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::StrokeSamplesTruncated);
    CHECK(result.error().offset == 8 + simple_stroke_chunk(1).size() + 6 + 26);
}

TEST_CASE("framing errors pass through parse_strokes") {
    CHECK(parse_page(file_header(1, {0, 0, 0, 0})).error().code == ErrorCode::StrokesBadMagic);
    CHECK(parse_page(file_header(0)).error().code == ErrorCode::StrokesVersionZero);

    CollectingSink sink;
    CHECK(parse_page(file_header(2), &sink).error().code == ErrorCode::StrokesVersionUnsupported);
    CHECK(sink.items().size() == 1);
}

TEST_CASE("flipping bytes in a valid file never crashes the parser") {
    StrokeHeaderIn header;
    header.flags = kFlagHasTilt | kFlagHasAzimuth;
    std::vector<SampleIn> samples;
    for (int i = 0; i < 20; ++i) samples.push_back(SampleIn{i * 10, i * 7, 100 + i, static_cast<std::uint64_t>(i * 3), i, -i, i * 100});
    header.stroke_id = 1;
    const Bytes valid = concat({file_header(), chunk(0x0001, body_of(header, samples)), simple_stroke_chunk(2), chunk(0x0002)});
    REQUIRE(parse_page(valid).has_value());

    std::mt19937_64 rng(777);
    for (int round = 0; round < 3000; ++round) {
        Bytes mutated = valid;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int f = 0; f < flips; ++f) mutated[rng() % mutated.size()] = static_cast<std::uint8_t>(rng());
        if (rng() % 4 == 0) mutated.resize(rng() % (mutated.size() + 1));
        const auto result = parse_page(mutated);
        if (result.has_value()) CHECK(result.value().size() <= 2);
    }
}
