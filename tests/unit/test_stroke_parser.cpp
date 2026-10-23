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
    header.flags = 0xFFE0;  // bits 5 to 15 are reserved (spec 7.4.3)
    const Stroke stroke = parse_ok(body_of(header, {SampleIn{}}));
    CHECK(stroke.flags.raw() == 0xFFE0);
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
    CHECK(parse_failure(filler(19)).code == ErrorCode::StrokeHeaderTooShort);
    CHECK(parse_failure({}).code == ErrorCode::StrokeHeaderTooShort);

    StrokeHeaderIn header;
    header.sample_count = 0;
    const StrokeParseError zero_count = parse_failure(stroke_header(header));
    CHECK(zero_count.code == ErrorCode::StrokeSampleCountZero);
    CHECK(zero_count.offset == 16);

    header.sample_count = 1;
    header.base_width = 0;
    const StrokeParseError zero_width = parse_failure(concat({stroke_header(header), zz(0), zz(0), zz(255), plain(0)}));
    CHECK(zero_width.code == ErrorCode::StrokeBaseWidthZero);
    CHECK(zero_width.offset == 10);
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
    CHECK(error.offset == 20);

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

Result<std::vector<InkItem>, PageError> parse_page(const Bytes& file, DiagnosticsSink* sink = nullptr) {
    return parse_ink(file, "p00003", sink);
}

const Stroke& stroke_at(const std::vector<InkItem>& ink, std::size_t index) { return std::get<Stroke>(ink.at(index)); }

}  // namespace

TEST_CASE("parse_ink returns the strokes in file order") {
    const Bytes file = concat({file_header(), simple_stroke_chunk(7, 10), chunk(0x0042, filler(5)),
                               simple_stroke_chunk(3, 20), chunk(0x0002)});
    const auto result = parse_page(file);
    REQUIRE(result.has_value());
    REQUIRE(result.value().size() == 2);
    CHECK(stroke_at(result.value(), 0).stroke_id == 7);
    CHECK(stroke_at(result.value(), 0).samples.x[0] == 10);
    CHECK(stroke_at(result.value(), 1).stroke_id == 3);
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
    CHECK(result.error().offset == 8 + simple_stroke_chunk(1).size() + 6 + 20);
}

TEST_CASE("framing errors pass through parse_ink") {
    CHECK(parse_page(file_header(2, {0, 0, 0, 0})).error().code == ErrorCode::StrokesBadMagic);
    CHECK(parse_page(file_header(0)).error().code == ErrorCode::StrokesVersionZero);

    // Every version other than 2 is a mismatch: the 0.2.0 layout (version 1) as well as a higher one.
    for (const std::uint16_t version : {std::uint16_t{1}, std::uint16_t{3}, std::uint16_t{0xFFFF}}) {
        CAPTURE(version);
        CollectingSink sink;
        CHECK(parse_page(file_header(version), &sink).error().code == ErrorCode::StrokesVersionUnsupported);
        REQUIRE(sink.items().size() == 1);
        CHECK(sink.items()[0].kind == DiagnosticKind::StrokesVersionMismatch);
    }
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

// ---------------------------------------------------------------------------
// ONF 0.3.0: layer, optional created_at, NO_TIMESTAMP and SHAPE chunks
// ---------------------------------------------------------------------------

namespace {

Shape shape_ok(const Bytes& body) {
    auto result = parse_shape(body);
    REQUIRE_MESSAGE(result.has_value(), "unexpected error: " << result.error().message);
    REQUIRE(result.value().has_value());
    return *std::move(result).value();
}

StrokeParseError shape_failure(const Bytes& body) {
    const auto result = parse_shape(body);
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

Bytes ellipse_body(std::int32_t rx = 3000, std::int32_t ry = 1500, std::uint16_t rotation = 4500) {
    ShapeIn in;
    in.shape_type = 1;
    in.parameters = ellipse_parameters(10500, 14850, rx, ry, rotation);
    return shape_body(in);
}

}  // namespace

TEST_CASE("stroke: layer is read from offset 14 and 0 is malformed") {
    for (const std::uint16_t layer : {std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{65535}}) {
        StrokeHeaderIn header;
        header.layer = layer;
        CHECK(parse_ok(body_of(header, {SampleIn{}})).layer == layer);
    }
    StrokeHeaderIn header;
    header.layer = 0;
    const StrokeParseError error = parse_failure(body_of(header, {SampleIn{}}));
    CHECK(error.code == ErrorCode::InkLayerZero);
    CHECK(error.offset == 14);
}

TEST_CASE("stroke: created_at is present only with HAS_CREATED_AT") {
    StrokeHeaderIn header;
    CHECK_FALSE(parse_ok(body_of(header, {SampleIn{}})).created_at.has_value());

    header.created_at = 1781433600000ull;
    const Stroke with = parse_ok(body_of(header, {SampleIn{5, 6, 7, 0}}));
    CHECK(with.created_at == 1781433600000ull);
    CHECK(with.flags.has_created_at());
    CHECK(with.samples.x == std::vector<Coord>{5});  // the samples start after the 8 bytes of created_at

    // created_at = 0 is a real value, not "unknown".
    header.created_at = 0;
    CHECK(parse_ok(body_of(header, {SampleIn{}})).created_at == 0ull);
}

TEST_CASE("stroke: a header cut inside created_at is too short") {
    StrokeHeaderIn header;
    header.created_at = 5;
    const Bytes full = stroke_header(header);
    REQUIRE(full.size() == 28);
    for (const std::size_t size : {std::size_t{20}, std::size_t{27}}) {
        CAPTURE(size);
        CHECK(parse_failure(Bytes(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size))).code ==
              ErrorCode::StrokeHeaderTooShort);
    }
}

TEST_CASE("stroke: NO_TIMESTAMP leaves out the timestamp field and the column stays empty") {
    StrokeHeaderIn header;
    header.flags = kFlagNoTimestamp | kFlagNoPressure;
    const std::vector<SampleIn> samples = {{10000, 5000, 255, 0}, {10004, 5003, 255, 0}};
    const Stroke stroke = parse_ok(body_of(header, samples));
    CHECK(stroke.flags.no_timestamp());
    CHECK(stroke.samples.timestamp.empty());
    CHECK(stroke.samples.size() == 2);
    CHECK(stroke.samples.x == std::vector<Coord>{10000, 10004});
    CHECK(stroke.samples.pressure == std::vector<std::uint8_t>{255, 255});

    // With tilt and azimuth the other fields keep their order.
    header.flags = kFlagNoTimestamp | kFlagHasTilt | kFlagHasAzimuth;
    const Stroke full = parse_ok(body_of(header, {SampleIn{1, 2, 3, 0, 4, -5, 600}, SampleIn{2, 3, 4, 0, 5, -6, 700}}));
    CHECK(full.samples.timestamp.empty());
    CHECK(full.samples.tilt_x == std::vector<std::int8_t>{4, 5});
    CHECK(full.samples.tilt_y == std::vector<std::int8_t>{-5, -6});
    CHECK(full.samples.azimuth == std::vector<std::uint16_t>{600, 700});
}

TEST_CASE("stroke: without timestamps a sample takes at least 3 bytes") {
    const Bytes three_bytes_each = filler(3 * 1000, 0);
    CHECK(parse_ok(body_with_count(1000, kFlagNoTimestamp, three_bytes_each)).samples.size() == 1000);
    CHECK(parse_failure(body_with_count(1001, kFlagNoTimestamp, three_bytes_each)).code == ErrorCode::StrokeSamplesTruncated);
    CHECK(parse_failure(body_with_count(1000, 0, three_bytes_each)).code == ErrorCode::StrokeSamplesTruncated);
    CHECK(parse_failure(body_with_count(std::numeric_limits<std::uint32_t>::max(), kFlagNoTimestamp, filler(16, 0))).code ==
          ErrorCode::StrokeSamplesTruncated);
}

TEST_CASE("stroke: a timestamp that cannot be decoded is only checked when the field exists") {
    // Without timestamps the 5th byte of a would-be timestamp is just the start of the next field.
    StrokeHeaderIn header;
    header.flags = kFlagNoTimestamp;
    header.sample_count = 2;
    const Bytes body = concat({stroke_header(header), zz(1), zz(2), zz(3), zz(1), zz(1), zz(1)});
    const Stroke stroke = parse_ok(body);
    CHECK(stroke.samples.x == std::vector<Coord>{1, 2});
}

TEST_CASE("shape: ellipse, rectangle, line and triangle") {
    ShapeIn in;
    in.stroke_id = 9;
    in.layer = 4;
    in.color = {1, 2, 3, 4};
    in.tool = 3;
    in.base_width = 400;

    in.shape_type = 1;
    in.parameters = ellipse_parameters(10500, 14850, 3000, 1500, 4500);
    const Shape ellipse = shape_ok(shape_body(in));
    CHECK(ellipse.stroke_id == 9);
    CHECK(ellipse.layer == 4);
    CHECK(ellipse.color == Color{1, 2, 3, 4});
    CHECK(ellipse.tool == ToolType::Highlighter);
    CHECK(ellipse.base_width == 400);
    CHECK_FALSE(ellipse.created_at.has_value());
    const Ellipse& e = std::get<Ellipse>(ellipse.geometry);
    CHECK(e.cx == 10500);
    CHECK(e.cy == 14850);
    CHECK(e.rx == 3000);
    CHECK(e.ry == 1500);
    CHECK(e.rotation == 4500);

    in.shape_type = 2;
    in.parameters = ellipse_parameters(-5, -6, 6000, 3000, 35999);  // same layout as the rectangle
    const Shape r_shape = shape_ok(shape_body(in));
    const Rectangle& r = std::get<Rectangle>(r_shape.geometry);
    CHECK(r.cx == -5);
    CHECK(r.cy == -6);
    CHECK(r.width == 6000);
    CHECK(r.height == 3000);
    CHECK(r.rotation == 35999);

    in.shape_type = 3;
    in.parameters = line_parameters(-500, 2000, 1000, -1000);
    const Shape l_shape = shape_ok(shape_body(in));
    const Line& l = std::get<Line>(l_shape.geometry);
    CHECK(l.x1 == -500);
    CHECK(l.y1 == 2000);
    CHECK(l.x2 == 1000);
    CHECK(l.y2 == -1000);

    in.shape_type = 4;
    in.parameters = triangle_parameters(5000, 5000, 8000, 5000, 6500, 7500);
    const Shape t_shape = shape_ok(shape_body(in));
    const Triangle& t = std::get<Triangle>(t_shape.geometry);
    CHECK(t.x3 == 6500);
    CHECK(t.y3 == 7500);

    // Collinear and identical points are valid (spec 7.9.4).
    in.parameters = triangle_parameters(1, 1, 1, 1, 1, 1);
    CHECK(shape_ok(shape_body(in)).stroke_id == 9);
    in.shape_type = 3;
    in.parameters = line_parameters(7, 7, 7, 7);
    CHECK(shape_ok(shape_body(in)).stroke_id == 9);
}

TEST_CASE("shape: coordinates use the full i32 range") {
    ShapeIn in;
    in.shape_type = 3;
    in.parameters = line_parameters(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), -1, 0);
    const Shape l_shape = shape_ok(shape_body(in));
    const Line& l = std::get<Line>(l_shape.geometry);
    CHECK(l.x1 == std::numeric_limits<std::int32_t>::min());
    CHECK(l.y1 == std::numeric_limits<std::int32_t>::max());
    CHECK(l.x2 == -1);
}

TEST_CASE("shape: created_at follows the fixed header") {
    ShapeIn in;
    in.created_at = 1781433600001ull;
    in.shape_type = 1;
    in.parameters = ellipse_parameters(1, 2, 3, 4, 5);
    const Shape shape = shape_ok(shape_body(in));
    CHECK(shape.created_at == 1781433600001ull);
    CHECK(std::get<Ellipse>(shape.geometry).cy == 2);

    const Bytes full = shape_body(in);
    CHECK(shape_failure(Bytes(full.begin(), full.begin() + 20)).code == ErrorCode::ShapeHeaderTooShort);
}

TEST_CASE("shape: tool type 4 is malformed, other unknown tool types are not") {
    ShapeIn in;
    in.shape_type = 1;
    in.parameters = ellipse_parameters(0, 0, 1, 1, 0);
    in.tool = 4;
    CHECK(shape_failure(shape_body(in)).code == ErrorCode::ShapeToolTypeInvalid);
    for (const std::uint16_t tool : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{3}}) {
        in.tool = tool;
        CHECK(shape_ok(shape_body(in)).tool_type_raw == tool);
    }
    in.tool = 77;  // unrecognised: rendered like a ballpoint pen (spec 7.4.2)
    const Shape unknown = shape_ok(shape_body(in));
    CHECK(unknown.tool == ToolType::Ballpoint);
    CHECK(unknown.tool_type_raw == 77);
}

TEST_CASE("shape: header and parameter errors") {
    ShapeIn in;
    in.shape_type = 1;
    in.parameters = ellipse_parameters(0, 0, 1, 1, 0);

    in.layer = 0;
    CHECK(shape_failure(shape_body(in)).code == ErrorCode::InkLayerZero);
    in.layer = 1;
    in.base_width = 0;
    CHECK(shape_failure(shape_body(in)).code == ErrorCode::StrokeBaseWidthZero);
    in.base_width = 100;

    CHECK(shape_failure(ellipse_body(0, 5)).code == ErrorCode::ShapeParameterInvalid);
    CHECK(shape_failure(ellipse_body(5, 0)).code == ErrorCode::ShapeParameterInvalid);
    CHECK(shape_failure(ellipse_body(-3, 5)).code == ErrorCode::ShapeParameterInvalid);
    CHECK(shape_failure(ellipse_body(5, 5, 36000)).code == ErrorCode::ShapeParameterInvalid);
    CHECK(shape_failure(ellipse_body(5, 5, 65535)).code == ErrorCode::ShapeParameterInvalid);
    CHECK(shape_ok(ellipse_body(5, 5, 35999)).stroke_id == 1);
    CHECK(shape_ok(ellipse_body(1, 1, 0)).stroke_id == 1);

    in.shape_type = 2;
    in.parameters = ellipse_parameters(0, 0, 0, 5, 0);
    CHECK(shape_failure(shape_body(in)).code == ErrorCode::ShapeParameterInvalid);
    in.parameters = ellipse_parameters(0, 0, 5, 5, 36000);
    CHECK(shape_failure(shape_body(in)).code == ErrorCode::ShapeParameterInvalid);
}

TEST_CASE("shape: truncated parameters are malformed, extra bytes are ignored") {
    ShapeIn in;
    for (const std::uint16_t type : {std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{3}, std::uint16_t{4}}) {
        CAPTURE(type);
        in.shape_type = type;
        const std::size_t size = type == 3 ? 16 : (type == 4 ? 24 : 18);
        in.parameters = Bytes(size, 0);
        if (type <= 2) {
            in.parameters = ellipse_parameters(0, 0, 1, 1, 0);
        }
        const Bytes full = shape_body(in);
        CHECK(parse_shape(full).has_value());

        const Bytes cut(full.begin(), full.end() - 1);
        CHECK(shape_failure(cut).code == ErrorCode::ShapeParametersTruncated);
        CHECK(shape_failure(Bytes(full.begin(), full.begin() + 18)).code == ErrorCode::ShapeParametersTruncated);

        Bytes padded = full;
        append(padded, filler(9, 0xAB));
        CHECK(parse_shape(padded).has_value());
    }
    // A body that ends before the shape_type.
    ShapeIn header_only;
    Bytes bytes = shape_body(header_only);
    CHECK(shape_failure(Bytes(bytes.begin(), bytes.begin() + 17)).code == ErrorCode::ShapeHeaderTooShort);
}

TEST_CASE("shape: an unknown shape type is skipped without looking at the rest") {
    for (const std::uint16_t type : {std::uint16_t{0}, std::uint16_t{5}, std::uint16_t{0x1234}, std::uint16_t{0xFFFF}}) {
        CAPTURE(type);
        ShapeIn in;
        in.shape_type = type;
        in.parameters = filler(3, 0xEE);
        in.layer = 0;       // not looked at
        in.base_width = 0;  // not looked at
        const auto result = parse_shape(shape_body(in));
        REQUIRE(result.has_value());
        CHECK_FALSE(result.value().has_value());
    }
}

TEST_CASE("ink: strokes and shapes keep the file order and share the stroke_id space") {
    StrokeHeaderIn stroke_header_in;
    stroke_header_in.stroke_id = 1;
    ShapeIn shape_in;
    shape_in.stroke_id = 2;
    shape_in.shape_type = 3;
    shape_in.parameters = line_parameters(0, 0, 10, 10);
    ShapeIn unknown_in;
    unknown_in.stroke_id = 3;
    unknown_in.shape_type = 99;

    const Bytes stroke = chunk(0x0001, body_of(stroke_header_in, {SampleIn{}}));
    const Bytes shape = chunk(0x0003, shape_body(shape_in));
    const Bytes unknown = chunk(0x0003, shape_body(unknown_in));

    const auto result = parse_page(concat({file_header(), shape, stroke, unknown, shape_in.stroke_id == 2 ? stroke : stroke, chunk(0x0002)}));
    REQUIRE_FALSE(result.has_value());  // the stroke is repeated
    CHECK(result.error().code == ErrorCode::StrokeIdDuplicate);

    const auto ok = parse_page(concat({file_header(), shape, stroke, unknown, chunk(0x0002)}));
    REQUIRE(ok.has_value());
    REQUIRE(ok.value().size() == 2);  // the unknown shape is dropped
    CHECK(std::holds_alternative<Shape>(ok.value()[0]));
    CHECK(std::holds_alternative<Stroke>(ok.value()[1]));

    // A shape with the id of a stroke is a duplicate.
    ShapeIn clash = shape_in;
    clash.stroke_id = 1;
    const auto duplicate = parse_page(concat({file_header(), stroke, chunk(0x0003, shape_body(clash))}));
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().code == ErrorCode::StrokeIdDuplicate);

    // An unknown shape does not take part in the uniqueness check.
    unknown_in.stroke_id = 1;
    CHECK(parse_page(concat({file_header(), stroke, chunk(0x0003, shape_body(unknown_in))})).has_value());
}

TEST_CASE("ink: a malformed shape makes the whole page malformed") {
    ShapeIn bad;
    bad.shape_type = 1;
    bad.parameters = ellipse_parameters(0, 0, 0, 1, 0);
    const auto result = parse_page(concat({file_header(), chunk(0x0003, shape_body(bad))}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::ShapeParameterInvalid);
    CHECK(result.error().page_id == "p00003");
}

TEST_CASE("flipping bytes in shape chunks never crashes the parser") {
    ShapeIn in;
    in.shape_type = 4;
    in.created_at = 5;
    in.parameters = triangle_parameters(1, 2, 3, 4, 5, 6);
    ShapeIn rect;
    rect.stroke_id = 2;
    rect.shape_type = 2;
    rect.parameters = ellipse_parameters(1, 2, 3, 4, 5);
    const Bytes valid = concat({file_header(), chunk(0x0003, shape_body(in)), chunk(0x0003, shape_body(rect)), chunk(0x0002)});
    REQUIRE(parse_page(valid).has_value());

    std::mt19937_64 rng(4242);
    for (int round = 0; round < 3000; ++round) {
        Bytes mutated = valid;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int f = 0; f < flips; ++f) mutated[rng() % mutated.size()] = static_cast<std::uint8_t>(rng());
        if (rng() % 4 == 0) mutated.resize(rng() % (mutated.size() + 1));
        const auto result = parse_page(mutated);
        if (result.has_value()) CHECK(result.value().size() <= 2);
    }
}
