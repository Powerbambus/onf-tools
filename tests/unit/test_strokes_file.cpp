#include "doctest.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "onf/diagnostics.h"
#include "onf/errors.h"
#include "onf/read/strokes_file.h"
#include "support/strokes_builder.h"

using namespace onf;
using namespace onf::test;

namespace {

constexpr std::uint16_t kStroke = 0x0001;
constexpr std::uint16_t kEnd = 0x0002;

Result<StrokeFile, PageError> parse(const Bytes& bytes, DiagnosticsSink* sink = nullptr) {
    return parse_strokes_file(bytes, "p00007", sink);
}

PageError parse_error(const Bytes& bytes) {
    const auto result = parse(bytes);
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

Bytes stroke_chunk(std::uint8_t marker = 0xEE) { return chunk(kStroke, filler(26, marker)); }

}  // namespace

TEST_CASE("a file with only the header is valid and has no strokes") {
    const auto result = parse(file_header());
    REQUIRE(result.has_value());
    CHECK(result.value().stroke_chunks.empty());
}

TEST_CASE("an END chunk is optional") {
    const auto without_end = parse(concat({file_header(), stroke_chunk()}));
    const auto with_end = parse(concat({file_header(), stroke_chunk(), chunk(kEnd)}));
    REQUIRE(without_end.has_value());
    REQUIRE(with_end.has_value());
    CHECK(without_end.value().stroke_chunks.size() == 1);
    CHECK(with_end.value().stroke_chunks.size() == 1);
}

TEST_CASE("files shorter than the header are rejected") {
    const Bytes header = file_header();
    for (std::size_t size = 0; size < header.size(); ++size) {
        CAPTURE(size);
        const Bytes cut(header.begin(), header.begin() + static_cast<std::ptrdiff_t>(size));
        const PageError error = parse_error(cut);
        CHECK(error.code == ErrorCode::StrokesTooShort);
        CHECK(error.page_id == "p00007");
        CHECK(error.offset == 0);
    }
}

TEST_CASE("wrong magic bytes are rejected before anything else") {
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        std::array<std::uint8_t, 4> magic = {0x4F, 0x4E, 0x46, 0x53};
        magic[i] ^= 0x01;
        CHECK(parse_error(file_header(1, magic)).code == ErrorCode::StrokesBadMagic);
    }
    CHECK(parse_error(file_header(0, {0, 0, 0, 0})).code == ErrorCode::StrokesBadMagic);
}

TEST_CASE("version 0 is malformed") {
    const PageError error = parse_error(file_header(0));
    CHECK(error.code == ErrorCode::StrokesVersionZero);
    CHECK(error.offset == 4);
}

TEST_CASE("a higher version is rejected and reported to the sink") {
    for (std::uint16_t version : std::vector<std::uint16_t>{2, 3, 65535}) {
        CAPTURE(version);
        CollectingSink sink;
        const auto result = parse(file_header(version), &sink);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::StrokesVersionUnsupported);
        REQUIRE(sink.items().size() == 1);
        CHECK(sink.items()[0].kind == DiagnosticKind::StrokesVersionMismatch);
        CHECK(sink.items()[0].page_id == "p00007");
    }
}

TEST_CASE("version 1 does not report anything") {
    CollectingSink sink;
    CHECK(parse(file_header(1), &sink).has_value());
    CHECK(sink.items().empty());
}

TEST_CASE("non-zero reserved bytes are ignored") {
    CHECK(parse(file_header(1, {0x4F, 0x4E, 0x46, 0x53}, {0xFF, 0xFF})).has_value());
}

TEST_CASE("1 to 5 bytes after the last chunk are an incomplete chunk header") {
    for (std::size_t extra = 1; extra <= 5; ++extra) {
        CAPTURE(extra);
        const PageError after_header = parse_error(concat({file_header(), filler(extra)}));
        CHECK(after_header.code == ErrorCode::ChunkHeaderTruncated);
        CHECK(after_header.offset == 8);

        const Bytes valid_part = concat({file_header(), stroke_chunk()});
        const PageError after_chunk = parse_error(concat({valid_part, filler(extra)}));
        CHECK(after_chunk.code == ErrorCode::ChunkHeaderTruncated);
        CHECK(after_chunk.offset == valid_part.size());
    }
}

TEST_CASE("a chunk length beyond the data is rejected") {
    const Bytes body = filler(26);

    const Bytes one_too_long = concat({file_header(), chunk_with_length(kStroke, 27, body)});
    CHECK(parse_error(one_too_long).code == ErrorCode::ChunkLengthExceedsData);

    const Bytes huge =
        concat({file_header(), chunk_with_length(kStroke, std::numeric_limits<std::uint32_t>::max(), body)});
    const PageError error = parse_error(huge);
    CHECK(error.code == ErrorCode::ChunkLengthExceedsData);
    CHECK(error.offset == 8);

    CHECK(parse(concat({file_header(), chunk_with_length(kStroke, 26, body)})).has_value());
}

TEST_CASE("a STROKE body must hold the 26-byte stroke header") {
    CHECK(parse_error(concat({file_header(), chunk(kStroke, filler(25))})).code == ErrorCode::StrokeHeaderTooShort);
    CHECK(parse_error(concat({file_header(), chunk(kStroke)})).code == ErrorCode::StrokeHeaderTooShort);
    CHECK(parse(concat({file_header(), chunk(kStroke, filler(26))})).has_value());
    CHECK(parse(concat({file_header(), chunk(kStroke, filler(100))})).has_value());
}

TEST_CASE("END stops reading and ignores its body and everything after it") {
    const Bytes garbage = filler(3);
    const Bytes file = concat({file_header(), stroke_chunk(0x01), chunk(kEnd, filler(10)), stroke_chunk(0x02),
                               chunk_with_length(kStroke, 9999, {}), garbage});
    const auto result = parse(file);
    REQUIRE(result.has_value());
    REQUIRE(result.value().stroke_chunks.size() == 1);
    CHECK(result.value().stroke_chunks[0].body[0] == 0x01);
}

TEST_CASE("unknown chunk types are skipped, including type 0") {
    for (std::uint16_t type : {std::uint16_t{0}, std::uint16_t{3}, std::uint16_t{0x1234}, std::uint16_t{0xFFFF}}) {
        CAPTURE(type);
        const Bytes file = concat({file_header(), chunk(type, filler(40)), stroke_chunk(0x01), chunk(type, filler(7)),
                                   stroke_chunk(0x02), chunk(type)});
        const auto result = parse(file);
        REQUIRE(result.has_value());
        REQUIRE(result.value().stroke_chunks.size() == 2);
        CHECK(result.value().stroke_chunks[0].body[0] == 0x01);
        CHECK(result.value().stroke_chunks[1].body[0] == 0x02);
    }
}

TEST_CASE("an unknown chunk with a bad length is still malformed") {
    const Bytes file = concat({file_header(), chunk_with_length(0x00FF, 100, filler(4))});
    CHECK(parse_error(file).code == ErrorCode::ChunkLengthExceedsData);
}

TEST_CASE("stroke chunks keep their order and point into the input") {
    const Bytes file = concat({file_header(), stroke_chunk(0x0A), stroke_chunk(0x0B), stroke_chunk(0x0C)});
    const auto result = parse(file);
    REQUIRE(result.has_value());

    const auto& chunks = result.value().stroke_chunks;
    REQUIRE(chunks.size() == 3);
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        CAPTURE(i);
        CHECK(chunks[i].body.size() == 26);
        CHECK(chunks[i].body.data() == file.data() + chunks[i].offset);
        CHECK(chunks[i].body[0] == 0x0A + i);
    }
    CHECK(chunks[0].offset == 8 + 6);
    CHECK(chunks[1].offset == 8 + 6 + 26 + 6);
}

TEST_CASE("cutting a valid file at any length never crashes and only gives framing errors") {
    const Bytes file = concat({file_header(), stroke_chunk(0x01), chunk(0x0042, filler(9)), stroke_chunk(0x02),
                               chunk(kEnd)});
    for (std::size_t size = 0; size <= file.size(); ++size) {
        CAPTURE(size);
        const Bytes cut(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = parse(cut);
        if (!result.has_value()) {
            const ErrorCode code = result.error().code;
            CHECK((code == ErrorCode::StrokesTooShort || code == ErrorCode::ChunkHeaderTruncated ||
                   code == ErrorCode::ChunkLengthExceedsData));
        }
    }
}
