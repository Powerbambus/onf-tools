#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
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

inline Bytes file_header(std::uint16_t version = 1, std::array<std::uint8_t, 4> magic = {0x4F, 0x4E, 0x46, 0x53},
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

struct StrokeHeaderIn {
    std::uint32_t stroke_id = 1;
    std::uint64_t created_at = 1000;
    std::array<std::uint8_t, 4> color = {1, 2, 3, 4};
    std::uint16_t tool = 0;
    std::uint16_t base_width = 100;
    std::uint16_t flags = 0;
    std::uint32_t sample_count = 1;
};

inline Bytes stroke_header(const StrokeHeaderIn& h) {
    Bytes out;
    append_u32(out, h.stroke_id);
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(h.created_at >> (8 * i)));
    out.insert(out.end(), h.color.begin(), h.color.end());
    append_u16(out, h.tool);
    append_u16(out, h.base_width);
    append_u16(out, h.flags);
    append_u32(out, h.sample_count);
    return out;
}

struct SampleIn {
    std::int64_t x = 0, y = 0, pressure = 255;
    std::uint64_t timestamp = 0;
    std::int64_t tilt_x = 0, tilt_y = 0, azimuth = 0;
};

constexpr std::uint16_t kFlagHasTilt = 1u << 0;
constexpr std::uint16_t kFlagHasAzimuth = 1u << 1;

// Absolute samples to the delta-encoded sample block (spec 7.5): all deltas against the previous sample, the first against 0.
inline Bytes encode_samples(const std::vector<SampleIn>& samples, std::uint16_t flags) {
    Bytes out;
    SampleIn prev;
    prev.pressure = 0;
    for (const SampleIn& s : samples) {
        append(out, zz(s.x - prev.x));
        append(out, zz(s.y - prev.y));
        append(out, zz(s.pressure - prev.pressure));
        append(out, plain(s.timestamp - prev.timestamp));
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
