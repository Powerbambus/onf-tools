#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

#include "support/varint_encoder.h"

namespace onf::test {

using Bytes = std::vector<std::uint8_t>;

inline void append_u16(Bytes& out, std::uint16_t v) {
    for (unsigned i = 0; i < 2; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void append_u32(Bytes& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void append(Bytes& out, std::span<const std::uint8_t> more) { out.insert(out.end(), more.begin(), more.end()); }

inline Bytes file_header(std::uint16_t version = 2, std::array<std::uint8_t, 4> magic = {0x4F, 0x4E, 0x46, 0x53},
                         std::array<std::uint8_t, 2> reserved = {0, 0}) {
    Bytes out(magic.begin(), magic.end());
    append_u16(out, version);
    out.insert(out.end(), reserved.begin(), reserved.end());
    return out;
}

inline Bytes chunk_with_length(std::uint16_t type, std::uint32_t declared_length, std::span<const std::uint8_t> body) {
    Bytes out;
    append_u16(out, type);
    append_u32(out, declared_length);
    append(out, body);
    return out;
}

inline Bytes chunk(std::uint16_t type, std::span<const std::uint8_t> body = {}) {
    return chunk_with_length(type, static_cast<std::uint32_t>(body.size()), body);
}

inline Bytes filler(std::size_t size, std::uint8_t value = 0xEE) { return Bytes(size, value); }

inline Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& part : parts) append(out, part);
    return out;
}

inline Bytes zz(std::int64_t value) { return encode_varint(zigzag_encode(value)); }
inline Bytes plain(std::uint64_t value) { return encode_varint(value); }

constexpr std::uint16_t kFlagHasTilt = 1u << 0;
constexpr std::uint16_t kFlagHasAzimuth = 1u << 1;
constexpr std::uint16_t kFlagNoPressure = 1u << 2;
constexpr std::uint16_t kFlagNoTimestamp = 1u << 3;
constexpr std::uint16_t kFlagHasCreatedAt = 1u << 4;

inline void append_u64(Bytes& out, std::uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// Stroke header of spec 7.4.1. created_at is written if (and only if) it is set, together with HAS_CREATED_AT.
struct StrokeHeaderIn {
    std::uint32_t stroke_id = 1;
    std::optional<std::uint64_t> created_at;
    std::array<std::uint8_t, 4> color = {1, 2, 3, 4};
    std::uint16_t tool = 0;
    std::uint16_t base_width = 100;
    std::uint16_t flags = 0;
    std::uint16_t layer = 1;
    std::uint32_t sample_count = 1;
};

inline Bytes stroke_header(const StrokeHeaderIn& h) {
    Bytes out;
    append_u32(out, h.stroke_id);
    out.insert(out.end(), h.color.begin(), h.color.end());
    append_u16(out, h.tool);
    append_u16(out, h.base_width);
    append_u16(out, static_cast<std::uint16_t>(h.created_at ? (h.flags | kFlagHasCreatedAt) : h.flags));
    append_u16(out, h.layer);
    append_u32(out, h.sample_count);
    if (h.created_at) append_u64(out, *h.created_at);
    return out;
}

// SHAPE chunk body of spec 7.9.2: header, optional created_at, shape_type and the raw parameter bytes.
struct ShapeIn {
    std::uint32_t stroke_id = 1;
    std::optional<std::uint64_t> created_at;
    std::array<std::uint8_t, 4> color = {1, 2, 3, 4};
    std::uint16_t tool = 0;
    std::uint16_t base_width = 100;
    std::uint16_t layer = 1;
    std::uint16_t shape_type = 1;
    Bytes parameters;
};

inline Bytes shape_body(const ShapeIn& h) {
    Bytes out;
    append_u32(out, h.stroke_id);
    out.insert(out.end(), h.color.begin(), h.color.end());
    append_u16(out, h.tool);
    append_u16(out, h.base_width);
    append_u16(out, h.created_at ? kFlagHasCreatedAt : 0);
    append_u16(out, h.layer);
    if (h.created_at) append_u64(out, *h.created_at);
    append_u16(out, h.shape_type);
    append(out, h.parameters);
    return out;
}

inline void append_i32(Bytes& out, std::int32_t v) { append_u32(out, static_cast<std::uint32_t>(v)); }

// Parameters of the four shape types (spec 7.9.4).
inline Bytes ellipse_parameters(std::int32_t cx, std::int32_t cy, std::int32_t rx, std::int32_t ry, std::uint16_t rotation) {
    Bytes out;
    for (const std::int32_t v : {cx, cy, rx, ry}) append_i32(out, v);
    append_u16(out, rotation);
    return out;
}

inline Bytes line_parameters(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2) {
    Bytes out;
    for (const std::int32_t v : {x1, y1, x2, y2}) append_i32(out, v);
    return out;
}

inline Bytes triangle_parameters(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::int32_t x3,
                                 std::int32_t y3) {
    Bytes out;
    for (const std::int32_t v : {x1, y1, x2, y2, x3, y3}) append_i32(out, v);
    return out;
}

struct SampleIn {
    std::int64_t x = 0, y = 0, pressure = 255;
    std::uint64_t timestamp = 0;
    std::int64_t tilt_x = 0, tilt_y = 0, azimuth = 0;
};

// Absolute samples to the delta-encoded sample block (spec 7.5): all deltas against the previous sample, the first against 0.
inline Bytes encode_samples(const std::vector<SampleIn>& samples, std::uint16_t flags) {
    Bytes out;
    SampleIn prev;
    prev.pressure = 0;
    for (const SampleIn& s : samples) {
        append(out, zz(s.x - prev.x));
        append(out, zz(s.y - prev.y));
        append(out, zz(s.pressure - prev.pressure));
        if (!(flags & kFlagNoTimestamp)) append(out, plain(s.timestamp - prev.timestamp));
        if (flags & kFlagHasTilt) {
            append(out, zz(s.tilt_x - prev.tilt_x));
            append(out, zz(s.tilt_y - prev.tilt_y));
        }
        if (flags & kFlagHasAzimuth) append(out, zz(s.azimuth - prev.azimuth));
        prev = s;
    }
    return out;
}

inline Bytes stroke_body(StrokeHeaderIn header, const std::vector<SampleIn>& samples) {
    header.sample_count = static_cast<std::uint32_t>(samples.size());
    return concat({stroke_header(header), encode_samples(samples, header.flags)});
}

}  // namespace onf::test
