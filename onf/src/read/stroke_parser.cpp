#include "onf/read/stroke_parser.h"

#include <array>
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
    stroke.color = {cursor.read_u8().value(), cursor.read_u8().value(), cursor.read_u8().value(),
                    cursor.read_u8().value()};
    stroke.tool_type_raw = cursor.read_u16().value();
    stroke.tool = effective_tool(stroke.tool_type_raw);
    stroke.base_width = cursor.read_u16().value();
    stroke.flags = StrokeFlags{cursor.read_u16().value()};
    stroke.layer = cursor.read_u16().value();
    const std::uint32_t sample_count = cursor.read_u32().value();

    if (stroke.flags.has_created_at()) {
        if (cursor.remaining() < limits::kCreatedAtSize) {
            return fail(ErrorCode::StrokeHeaderTooShort, "STROKE chunk is shorter than the stroke header with created_at",
                        cursor.position());
        }
        stroke.created_at = cursor.read_u64().value();
    }

    if (stroke.layer == 0) return fail(ErrorCode::InkLayerZero, "layer is 0", 14);
    if (sample_count == 0) return fail(ErrorCode::StrokeSampleCountZero, "sample_count is 0", 16);
    if (stroke.base_width == 0) return fail(ErrorCode::StrokeBaseWidthZero, "base_width is 0", 10);

    const bool with_timestamp = !stroke.flags.no_timestamp();
    const std::uint64_t min_sample_size = limits::kMinSampleSize + (with_timestamp ? limits::kTimestampMinSize : 0) +
                                          (stroke.flags.has_tilt() ? 2 : 0) + (stroke.flags.has_azimuth() ? 1 : 0);
    if (sample_count > cursor.remaining() / min_sample_size) {
        return fail(ErrorCode::StrokeSamplesTruncated, "the samples need more bytes than the chunk body has",
                    cursor.position());
    }

    SampleColumns& samples = stroke.samples;
    samples.x.reserve(sample_count);
    samples.y.reserve(sample_count);
    samples.pressure.reserve(sample_count);
    if (with_timestamp) samples.timestamp.reserve(sample_count);
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
        if (with_timestamp) {
            if (auto e = decoder.plain(timestamp, static_cast<std::int64_t>(limits::kTimestampMaxValue), "timestamp")) {
                return unexpected(std::move(*e));
            }
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
        if (with_timestamp) samples.timestamp.push_back(static_cast<Timestamp>(timestamp));
        if (stroke.flags.has_tilt()) {
            samples.tilt_x.push_back(static_cast<std::int8_t>(tilt_x));
            samples.tilt_y.push_back(static_cast<std::int8_t>(tilt_y));
        }
        if (stroke.flags.has_azimuth()) samples.azimuth.push_back(static_cast<std::uint16_t>(azimuth));
    }
    return stroke;
}

namespace {

constexpr std::uint16_t kShapeEllipse = 1;
constexpr std::uint16_t kShapeRectangle = 2;
constexpr std::uint16_t kShapeLine = 3;
constexpr std::uint16_t kShapeTriangle = 4;

// Size of the parameters of a known shape type, 0 for any other value (spec 7.9.3).
std::uint64_t parameter_size(std::uint16_t shape_type) noexcept {
    switch (shape_type) {
        case kShapeEllipse:   return limits::kEllipseParameterSize;
        case kShapeRectangle: return limits::kRectangleParameterSize;
        case kShapeLine:      return limits::kLineParameterSize;
        case kShapeTriangle:  return limits::kTriangleParameterSize;
        default:              return 0;
    }
}

// Reads n coord values; the caller has checked that the bytes are there.
template <std::size_t N>
std::array<Coord, N> read_coords(ByteCursor& cursor) {
    std::array<Coord, N> out{};
    for (Coord& value : out) value = static_cast<Coord>(cursor.read_u32().value());
    return out;
}

}  // namespace

Result<std::optional<Shape>, StrokeParseError> parse_shape(std::span<const std::uint8_t> body) {
    if (body.size() < limits::kShapeHeaderSize) {
        return fail(ErrorCode::ShapeHeaderTooShort, "SHAPE chunk is shorter than the shape header", 0);
    }

    ByteCursor cursor{body};
    Shape shape;
    shape.stroke_id = cursor.read_u32().value();
    shape.color = {cursor.read_u8().value(), cursor.read_u8().value(), cursor.read_u8().value(),
                   cursor.read_u8().value()};
    shape.tool_type_raw = cursor.read_u16().value();
    shape.tool = effective_tool(shape.tool_type_raw);
    shape.base_width = cursor.read_u16().value();
    const StrokeFlags flags{cursor.read_u16().value()};  // only HAS_CREATED_AT is defined for shapes
    shape.layer = cursor.read_u16().value();

    if (flags.has_created_at()) {
        if (cursor.remaining() < limits::kCreatedAtSize) {
            return fail(ErrorCode::ShapeHeaderTooShort, "SHAPE chunk is shorter than the shape header with created_at",
                        cursor.position());
        }
        shape.created_at = cursor.read_u64().value();
    }
    if (cursor.remaining() < limits::kShapeTypeSize) {
        return fail(ErrorCode::ShapeHeaderTooShort, "SHAPE chunk has no shape_type", cursor.position());
    }
    const std::uint16_t shape_type = cursor.read_u16().value();

    const std::uint64_t needed = parameter_size(shape_type);
    if (needed == 0) return std::optional<Shape>{};  // unknown shape type: skip the chunk

    if (cursor.remaining() < needed) {
        return fail(ErrorCode::ShapeParametersTruncated, "SHAPE chunk is shorter than the parameters of its shape type",
                    cursor.position());
    }
    if (shape.layer == 0) return fail(ErrorCode::InkLayerZero, "layer is 0", 14);
    if (shape.base_width == 0) return fail(ErrorCode::StrokeBaseWidthZero, "base_width is 0", 10);

    const std::uint64_t parameters_offset = cursor.position();
    switch (shape_type) {
        case kShapeEllipse: {
            const auto c = read_coords<4>(cursor);
            const std::uint16_t rotation = cursor.read_u16().value();
            if (c[2] <= 0 || c[3] <= 0) return fail(ErrorCode::ShapeParameterInvalid, "rx and ry must be greater than 0", parameters_offset + 8);
            if (rotation > limits::kMaxRotation) return fail(ErrorCode::ShapeParameterInvalid, "rotation must be 0 to 35999", parameters_offset + 16);
            shape.geometry = Ellipse{c[0], c[1], c[2], c[3], rotation};
            break;
        }
        case kShapeRectangle: {
            const auto c = read_coords<4>(cursor);
            const std::uint16_t rotation = cursor.read_u16().value();
            if (c[2] <= 0 || c[3] <= 0) return fail(ErrorCode::ShapeParameterInvalid, "width and height must be greater than 0", parameters_offset + 8);
            if (rotation > limits::kMaxRotation) return fail(ErrorCode::ShapeParameterInvalid, "rotation must be 0 to 35999", parameters_offset + 16);
            shape.geometry = Rectangle{c[0], c[1], c[2], c[3], rotation};
            break;
        }
        case kShapeLine: {
            const auto c = read_coords<4>(cursor);
            shape.geometry = Line{c[0], c[1], c[2], c[3]};
            break;
        }
        default: {  // kShapeTriangle
            const auto c = read_coords<6>(cursor);
            shape.geometry = Triangle{c[0], c[1], c[2], c[3], c[4], c[5]};
            break;
        }
    }
    // Bytes left in the body after the parameters are reserved and ignored (spec 7.3.2).
    return std::optional<Shape>{std::move(shape)};
}

Result<std::vector<InkItem>, PageError> parse_ink(std::span<const std::uint8_t> bytes, std::string_view page_id,
                                                  DiagnosticsSink* sink) {
    auto file = parse_strokes_file(bytes, page_id, sink);
    if (!file) return unexpected(std::move(file).error());

    std::vector<InkItem> ink;
    ink.reserve(file.value().ink_chunks.size());
    std::unordered_set<std::uint32_t> seen_ids;

    const auto page_failure = [&](StrokeParseError& e, const InkChunk& chunk) {
        return unexpected(PageError{std::string(page_id), e.code, std::move(e.message), chunk.offset + e.offset});
    };
    const auto duplicate = [&](std::uint32_t id, const InkChunk& chunk) {
        return unexpected(PageError{std::string(page_id), ErrorCode::StrokeIdDuplicate,
                                    "duplicate stroke_id " + std::to_string(id), chunk.offset});
    };

    for (const InkChunk& chunk : file.value().ink_chunks) {
        if (chunk.type == kChunkTypeStroke) {
            auto stroke = parse_stroke(chunk.body);
            if (!stroke) return page_failure(stroke.error(), chunk);
            if (!seen_ids.insert(stroke.value().stroke_id).second) return duplicate(stroke.value().stroke_id, chunk);
            // Tool type 4 was the line eraser of ONF 0.3.0 and 0.3.1: the chunk was parsed and checked like any
            // other, the stroke is skipped (spec 7.4.2).
            if (stroke.value().tool_type_raw == kRemovedToolType) continue;
            ink.push_back(std::move(stroke).value());
        } else {
            auto shape = parse_shape(chunk.body);
            if (!shape) return page_failure(shape.error(), chunk);
            if (!shape.value()) continue;  // unknown shape type
            if (!seen_ids.insert(shape.value()->stroke_id).second) return duplicate(shape.value()->stroke_id, chunk);
            if (shape.value()->tool_type_raw == kRemovedToolType) continue;  // see above
            ink.push_back(std::move(*shape.value()));
        }
    }
    return ink;
}

}  // namespace onf
