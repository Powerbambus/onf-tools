#include "onf/read/stroke_parser.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "onf/limits.h"
#include "onf/primitives/bytecursor.h"
#include "onf/primitives/varint.h"
#include "onf/read/strokes_file.h"

namespace onf {

namespace {

Unexpected<StrokeParseError> fail(ErrorCode code, std::string message, std::uint64_t offset) {
    return unexpected(StrokeParseError{code, std::move(message), offset});
}

// A varint that runs past the end of the chunk body means the samples need more bytes than the body has.
Unexpected<StrokeParseError> varint_failure(ErrorCode code, std::uint64_t offset) {
    if (code == ErrorCode::VarintTruncated) {
        return fail(ErrorCode::StrokeSamplesTruncated, "sample data ends inside a varint", offset);
    }
    return fail(code, "malformed varint in sample data", offset);
}

struct SampleDecoder {
    ByteCursor& cursor;
    std::uint32_t sample_index = 0;

    std::optional<StrokeParseError> zigzag(std::int64_t& accumulated, std::int64_t min, std::int64_t max,
                                           std::string_view field) {
        const std::uint64_t offset = cursor.position();
        const auto raw = decode_varint(cursor, limits::kVarintMaxValue);
        if (!raw) return varint_failure(raw.error(), offset).error();

        accumulated += zigzag_decode(raw.value());
        return check_range(accumulated, min, max, field, offset);
    }

    std::optional<StrokeParseError> plain(std::int64_t& accumulated, std::int64_t max, std::string_view field) {
        const std::uint64_t offset = cursor.position();
        const auto raw = decode_varint(cursor, limits::kTimestampMaxValue);
        if (!raw) return varint_failure(raw.error(), offset).error();

        accumulated += static_cast<std::int64_t>(raw.value());
        return check_range(accumulated, 0, max, field, offset);
    }

private:
    std::optional<StrokeParseError> check_range(std::int64_t value, std::int64_t min, std::int64_t max,
                                                std::string_view field, std::uint64_t offset) const {
        if (value >= min && value <= max) return std::nullopt;
        return StrokeParseError{ErrorCode::SampleValueOutOfRange,
                                "sample " + std::to_string(sample_index) + ": " + std::string(field) + " out of range",
                                offset};
    }
};

}  // namespace

Result<Stroke, StrokeParseError> parse_stroke(std::span<const std::uint8_t> body) {
    if (body.size() < limits::kStrokeHeaderSize) {
        return fail(ErrorCode::StrokeHeaderTooShort, "STROKE chunk is shorter than the stroke header", 0);
    }

    ByteCursor cursor{body};
    Stroke stroke;
    stroke.stroke_id = cursor.read_u32().value();
    stroke.created_at = cursor.read_u64().value();
    stroke.color = {cursor.read_u8().value(), cursor.read_u8().value(), cursor.read_u8().value(),
                    cursor.read_u8().value()};
    stroke.tool_type_raw = cursor.read_u16().value();
    stroke.tool = effective_tool(stroke.tool_type_raw);
    stroke.base_width = cursor.read_u16().value();
    stroke.flags = StrokeFlags{cursor.read_u16().value()};
    const std::uint32_t sample_count = cursor.read_u32().value();

    if (sample_count == 0) return fail(ErrorCode::StrokeSampleCountZero, "sample_count is 0", 22);
    if (stroke.base_width == 0) return fail(ErrorCode::StrokeBaseWidthZero, "base_width is 0", 18);

    const std::uint64_t min_sample_size = limits::kMinSampleSize + (stroke.flags.has_tilt() ? 2 : 0) +
                                          (stroke.flags.has_azimuth() ? 1 : 0);
    if (sample_count > cursor.remaining() / min_sample_size) {
        return fail(ErrorCode::StrokeSamplesTruncated, "the samples need more bytes than the chunk body has",
                    cursor.position());
    }

    SampleColumns& samples = stroke.samples;
    samples.x.reserve(sample_count);
    samples.y.reserve(sample_count);
    samples.pressure.reserve(sample_count);
    samples.timestamp.reserve(sample_count);
    if (stroke.flags.has_tilt()) {
        samples.tilt_x.reserve(sample_count);
        samples.tilt_y.reserve(sample_count);
    }
    if (stroke.flags.has_azimuth()) samples.azimuth.reserve(sample_count);

    // The first sample is a delta against zero, so all accumulators start at zero (spec 7.5.2).
    std::int64_t x = 0, y = 0, pressure = 0, timestamp = 0, tilt_x = 0, tilt_y = 0, azimuth = 0;
    SampleDecoder decoder{cursor};

    for (std::uint32_t i = 0; i < sample_count; ++i) {
        decoder.sample_index = i;

        if (auto e = decoder.zigzag(x, INT32_MIN, INT32_MAX, "x")) return unexpected(std::move(*e));
        if (auto e = decoder.zigzag(y, INT32_MIN, INT32_MAX, "y")) return unexpected(std::move(*e));
        if (auto e = decoder.zigzag(pressure, 0, 255, "pressure")) return unexpected(std::move(*e));
        if (auto e = decoder.plain(timestamp, static_cast<std::int64_t>(limits::kTimestampMaxValue), "timestamp")) {
            return unexpected(std::move(*e));
        }
        if (stroke.flags.has_tilt()) {
            if (auto e = decoder.zigzag(tilt_x, -90, 90, "tilt_x")) return unexpected(std::move(*e));
            if (auto e = decoder.zigzag(tilt_y, -90, 90, "tilt_y")) return unexpected(std::move(*e));
        }
        if (stroke.flags.has_azimuth()) {
            if (auto e = decoder.zigzag(azimuth, 0, 35999, "azimuth")) return unexpected(std::move(*e));
        }

        samples.x.push_back(static_cast<Coord>(x));
        samples.y.push_back(static_cast<Coord>(y));
        samples.pressure.push_back(static_cast<std::uint8_t>(pressure));
        samples.timestamp.push_back(static_cast<Timestamp>(timestamp));
        if (stroke.flags.has_tilt()) {
            samples.tilt_x.push_back(static_cast<std::int8_t>(tilt_x));
            samples.tilt_y.push_back(static_cast<std::int8_t>(tilt_y));
        }
        if (stroke.flags.has_azimuth()) samples.azimuth.push_back(static_cast<std::uint16_t>(azimuth));
    }
    return stroke;
}

Result<std::vector<Stroke>, PageError> parse_strokes(std::span<const std::uint8_t> bytes, std::string_view page_id,
                                                     DiagnosticsSink* sink) {
    auto file = parse_strokes_file(bytes, page_id, sink);
    if (!file) return unexpected(std::move(file).error());

    std::vector<Stroke> strokes;
    strokes.reserve(file.value().stroke_chunks.size());
    std::unordered_set<std::uint32_t> seen_ids;

    for (const StrokeChunk& chunk : file.value().stroke_chunks) {
        auto stroke = parse_stroke(chunk.body);
        if (!stroke) {
            StrokeParseError& e = stroke.error();
            return unexpected(PageError{std::string(page_id), e.code, std::move(e.message), chunk.offset + e.offset});
        }
        if (!seen_ids.insert(stroke.value().stroke_id).second) {
            return unexpected(PageError{std::string(page_id), ErrorCode::StrokeIdDuplicate,
                                        "duplicate stroke_id " + std::to_string(stroke.value().stroke_id), chunk.offset});
        }
        strokes.push_back(std::move(stroke).value());
    }
    return strokes;
}

}  // namespace onf
