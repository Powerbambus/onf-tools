#include "onf/write/strokes.h"

#include <cstddef>
#include <variant>

#include "onf/read/strokes_file.h"
#include "onf/primitives/varint.h"

namespace onf {

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put_u32(Bytes& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put_u64(Bytes& out, std::uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put_i32(Bytes& out, std::int32_t v) { put_u32(out, static_cast<std::uint32_t>(v)); }

void put_color(Bytes& out, const Color& c) {
    out.push_back(c.red);
    out.push_back(c.green);
    out.push_back(c.blue);
    out.push_back(c.alpha);
}

void put_chunk(Bytes& out, std::uint16_t type, const Bytes& body) {
    put_u16(out, type);
    put_u32(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

constexpr std::uint16_t kHasCreatedAt = 1u << 4;
constexpr std::uint16_t kDefinedStrokeFlags = 0x007F;

// Spec 7.4.1 and 7.5: header, created_at if known, and the delta-coded samples.
Bytes stroke_body(const Stroke& stroke) {
    Bytes out;
    std::uint16_t flags = static_cast<std::uint16_t>(stroke.flags.raw() & kDefinedStrokeFlags & ~kHasCreatedAt);
    if (stroke.created_at) flags = static_cast<std::uint16_t>(flags | kHasCreatedAt);
    const StrokeFlags f{flags};
    const SampleColumns& s = stroke.samples;

    put_u32(out, stroke.stroke_id);
    put_color(out, stroke.color);
    put_u16(out, stroke.tool_type_raw);
    put_u16(out, stroke.base_width);
    put_u16(out, flags);
    put_u16(out, stroke.layer);
    put_u32(out, static_cast<std::uint32_t>(s.size()));
    if (stroke.created_at) put_u64(out, *stroke.created_at);

    std::int64_t x = 0, y = 0, pressure = 0, timestamp = 0, tilt_x = 0, tilt_y = 0, azimuth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        append_varint(out, zigzag_to_unsigned(static_cast<std::int64_t>(s.x[i]) - x));
        append_varint(out, zigzag_to_unsigned(static_cast<std::int64_t>(s.y[i]) - y));
        append_varint(out, zigzag_to_unsigned(static_cast<std::int64_t>(s.pressure[i]) - pressure));
        x = s.x[i];
        y = s.y[i];
        pressure = s.pressure[i];
        if (!f.no_timestamp()) {
            append_varint(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(s.timestamp[i]) - timestamp));
            timestamp = s.timestamp[i];
        }
        if (f.has_tilt()) {
            append_varint(out, zigzag_to_unsigned(static_cast<std::int64_t>(s.tilt_x[i]) - tilt_x));
            append_varint(out, zigzag_to_unsigned(static_cast<std::int64_t>(s.tilt_y[i]) - tilt_y));
            tilt_x = s.tilt_x[i];
            tilt_y = s.tilt_y[i];
        }
        if (f.has_azimuth()) {
            append_varint(out, zigzag_to_unsigned(static_cast<std::int64_t>(s.azimuth[i]) - azimuth));
            azimuth = s.azimuth[i];
        }
    }
    return out;
}

// Spec 7.9.2 to 7.9.4.
Bytes shape_body(const Shape& shape) {
    Bytes out;
    put_u32(out, shape.stroke_id);
    put_color(out, shape.color);
    put_u16(out, shape.tool_type_raw);
    put_u16(out, shape.base_width);
    put_u16(out, shape.created_at ? kHasCreatedAt : 0);
    put_u16(out, shape.layer);
    if (shape.created_at) put_u64(out, *shape.created_at);

    struct Visitor {
        Bytes& out;
        void operator()(const Ellipse& e) const {
            put_u16(out, 1);
            for (const Coord v : {e.cx, e.cy, e.rx, e.ry}) put_i32(out, v);
            put_u16(out, e.rotation);
        }
        void operator()(const Rectangle& r) const {
            put_u16(out, 2);
            for (const Coord v : {r.cx, r.cy, r.width, r.height}) put_i32(out, v);
            put_u16(out, r.rotation);
        }
        void operator()(const Line& l) const {
            put_u16(out, 3);
            for (const Coord v : {l.x1, l.y1, l.x2, l.y2}) put_i32(out, v);
        }
        void operator()(const Triangle& t) const {
            put_u16(out, 4);
            for (const Coord v : {t.x1, t.y1, t.x2, t.y2, t.x3, t.y3}) put_i32(out, v);
        }
    };
    std::visit(Visitor{out}, shape.geometry);
    return out;
}

}  // namespace

std::vector<std::uint8_t> encode_strokes_file(const std::vector<InkItem>& ink) {
    Bytes out = {'O', 'N', 'F', 'S'};
    put_u16(out, kStrokesFormatVersion);
    put_u16(out, 0);  // reserved

    for (const InkItem& item : ink) {
        if (const Stroke* stroke = std::get_if<Stroke>(&item)) {
            put_chunk(out, kChunkTypeStroke, stroke_body(*stroke));
        } else {
            put_chunk(out, kChunkTypeShape, shape_body(std::get<Shape>(item)));
        }
    }
    put_chunk(out, kChunkTypeEnd, {});
    return out;
}

}  // namespace onf
