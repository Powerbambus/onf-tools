#include "doctest.h"

#include <string>
#include <variant>

#include "onf/read/stroke_parser.h"
#include "support/strokes_builder.h"
#include "support/vectors.h"

using namespace onf;
using namespace onf::test;

namespace {

// Chunk vectors V1 to V7 of spec Appendix C, extracted from the specification (tests/vectors/ink_chunks.tsv).
Bytes vector_chunk(const std::string& name) {
    for (const Row& row : load_vectors("ink_chunks.tsv")) {
        if (row.at(0) == name) return parse_hex_bytes(row.at(2));
    }
    FAIL("no vector " << name);
    return {};
}

std::vector<InkItem> parse_vectors(std::initializer_list<const char*> names) {
    Bytes file = file_header();
    for (const char* name : names) append(file, vector_chunk(name));
    append(file, chunk(0x0002));
    auto result = parse_ink(file, "p00001");
    REQUIRE_MESSAGE(result.has_value(), result.error().message);
    return std::move(result).value();
}

}  // namespace

TEST_CASE("vectors: the chunk frame and the length of every vector") {
    for (const Row& row : load_vectors("ink_chunks.tsv")) {
        CAPTURE(row.at(0));
        const Bytes bytes = parse_hex_bytes(row.at(2));
        REQUIRE(bytes.size() >= 6);
        CHECK(bytes[0] == std::stoul(row.at(1)));
        CHECK(bytes[1] == 0);
        const std::size_t length = bytes[2] | (bytes[3] << 8) | (bytes[4] << 16) | (static_cast<std::size_t>(bytes[5]) << 24);
        CHECK(length == bytes.size() - 6);
    }
}

TEST_CASE("vectors: V1 stroke without pressure and timestamps, layer 2") {
    const auto ink = parse_vectors({"V1"});
    REQUIRE(ink.size() == 1);
    const Stroke& s = std::get<Stroke>(ink[0]);
    CHECK(s.stroke_id == 1);
    CHECK(s.layer == 2);
    CHECK(s.color == Color{0, 0, 0, 255});
    CHECK(s.tool == ToolType::Ballpoint);
    CHECK(s.base_width == 100);
    CHECK(s.flags.raw() == 0x000C);
    CHECK_FALSE(s.created_at.has_value());
    CHECK(s.samples.x == std::vector<Coord>{10000, 10004});
    CHECK(s.samples.y == std::vector<Coord>{5000, 5003});
    CHECK(s.samples.pressure == std::vector<std::uint8_t>{255, 255});
    CHECK(s.samples.timestamp.empty());
}

TEST_CASE("vectors: V2 stroke with created_at and timestamps, layer 1") {
    const auto ink = parse_vectors({"V2"});
    REQUIRE(ink.size() == 1);
    const Stroke& s = std::get<Stroke>(ink[0]);
    CHECK(s.stroke_id == 2);
    CHECK(s.layer == 1);
    CHECK(s.tool == ToolType::Fountain);
    CHECK(s.created_at == 1781433600000ull);
    CHECK(s.samples.x == std::vector<Coord>{10000, 10004});
    CHECK(s.samples.pressure == std::vector<std::uint8_t>{200, 202});
    CHECK(s.samples.timestamp == std::vector<Timestamp>{0, 8});
}

TEST_CASE("vectors: V3 ellipse") {
    const auto ink = parse_vectors({"V3"});
    const Shape& s = std::get<Shape>(ink.at(0));
    CHECK(s.stroke_id == 3);
    CHECK(s.layer == 2);
    CHECK(s.color == Color{255, 0, 0, 255});
    CHECK_FALSE(s.created_at.has_value());
    const Ellipse& e = std::get<Ellipse>(s.geometry);
    CHECK(e.cx == 10500);
    CHECK(e.cy == 14850);
    CHECK(e.rx == 3000);
    CHECK(e.ry == 1500);
    CHECK(e.rotation == 4500);
}

TEST_CASE("vectors: V4 rectangle with created_at and a highlighter alpha") {
    const auto ink = parse_vectors({"V4"});
    const Shape& s = std::get<Shape>(ink.at(0));
    CHECK(s.stroke_id == 4);
    CHECK(s.layer == 3);
    CHECK(s.tool == ToolType::Highlighter);
    CHECK(s.color == Color{0, 0, 0, 0x66});
    CHECK(s.base_width == 400);
    CHECK(s.created_at == 1781433600001ull);
    const Rectangle& r = std::get<Rectangle>(s.geometry);
    CHECK(r.cx == 10500);
    CHECK(r.cy == 14850);
    CHECK(r.width == 6000);
    CHECK(r.height == 3000);
    CHECK(r.rotation == 9000);
}

TEST_CASE("vectors: V5 line with negative coordinates") {
    const auto ink = parse_vectors({"V5"});
    const Shape& s = std::get<Shape>(ink.at(0));
    CHECK(s.stroke_id == 5);
    CHECK(s.layer == 1);
    const Line& l = std::get<Line>(s.geometry);
    CHECK(l.x1 == -500);
    CHECK(l.y1 == 2000);
    CHECK(l.x2 == 1000);
    CHECK(l.y2 == -1000);
}

TEST_CASE("vectors: V6 triangle") {
    const auto ink = parse_vectors({"V6"});
    const Shape& s = std::get<Shape>(ink.at(0));
    CHECK(s.stroke_id == 6);
    CHECK(s.tool == ToolType::Pencil);
    CHECK(s.base_width == 150);
    const Triangle& t = std::get<Triangle>(s.geometry);
    CHECK(t.x1 == 5000);
    CHECK(t.y1 == 5000);
    CHECK(t.x2 == 8000);
    CHECK(t.y2 == 5000);
    CHECK(t.x3 == 6500);
    CHECK(t.y3 == 7500);
}

TEST_CASE("vectors: V7 fountain pen stroke without taper at both ends") {
    const auto ink = parse_vectors({"V7"});
    REQUIRE(ink.size() == 1);
    const Stroke& s = std::get<Stroke>(ink[0]);
    CHECK(s.stroke_id == 7);
    CHECK(s.layer == 1);
    CHECK(s.tool == ToolType::Fountain);
    CHECK(s.base_width == 100);
    CHECK(s.flags.raw() == 0x006C);
    CHECK(s.flags.no_pressure());
    CHECK(s.flags.no_timestamp());
    CHECK(s.flags.no_taper_start());
    CHECK(s.flags.no_taper_end());
    CHECK(s.samples.x == std::vector<Coord>{1000, 1010});
    CHECK(s.samples.y == std::vector<Coord>{2000, 2005});
    CHECK(s.samples.pressure == std::vector<std::uint8_t>{255, 255});
    CHECK(s.samples.timestamp.empty());
}

TEST_CASE("vectors: all seven in one file keep the order") {
    const auto ink = parse_vectors({"V1", "V2", "V7", "V3", "V4", "V5", "V6"});
    REQUIRE(ink.size() == 7);
    for (std::size_t i = 0; i < 3; ++i) CHECK(std::holds_alternative<Stroke>(ink[i]));
    for (std::size_t i = 3; i < 7; ++i) CHECK(std::holds_alternative<Shape>(ink[i]));
}

TEST_CASE("vectors: the size effect of NO_TIMESTAMP (spec C.3)") {
    StrokeHeaderIn header;
    header.layer = 1;
    const std::vector<SampleIn> samples = {{0, 0, 255, 0}, {1, 1, 255, 1}, {2, 2, 255, 2}};
    header.flags = kFlagNoPressure;
    const std::size_t with_timestamps = chunk(0x0001, stroke_body(header, samples)).size();
    header.flags = kFlagNoPressure | kFlagNoTimestamp;
    const std::size_t without = chunk(0x0001, stroke_body(header, samples)).size();
    CHECK(with_timestamps == 39);
    CHECK(without == 36);
}
