#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "onf/model/types.h"

namespace onf {

enum class ToolType : std::uint16_t {
    Ballpoint = 0,
    Fountain = 1,
    Pencil = 2,
    Highlighter = 3,
    LineEraser = 4,
};

// Spec 7.4.2: an unrecognised tool type is treated as a ballpoint pen.
constexpr ToolType effective_tool(std::uint16_t raw) noexcept {
    return raw <= static_cast<std::uint16_t>(ToolType::LineEraser) ? static_cast<ToolType>(raw) : ToolType::Ballpoint;
}

// One vector per field. timestamp is empty when the stroke has NO_TIMESTAMP (the elapsed time is unknown, never 0,
// spec 5.3). tilt_x, tilt_y and azimuth are empty when the stroke has no such fields.
struct SampleColumns {
    std::vector<Coord> x;
    std::vector<Coord> y;
    std::vector<std::uint8_t> pressure;
    std::vector<Timestamp> timestamp;
    std::vector<std::int8_t> tilt_x;
    std::vector<std::int8_t> tilt_y;
    std::vector<std::uint16_t> azimuth;  // hundredths of a degree

    std::size_t size() const noexcept { return x.size(); }
};

struct Stroke {
    std::uint32_t stroke_id = 0;
    std::uint16_t layer = 0;              // 1 to 65535, spec 5.6
    std::optional<MsTime> created_at;     // absent means unknown, spec 5.5
    Color color;
    std::uint16_t tool_type_raw = 0;
    ToolType tool = ToolType::Ballpoint;
    std::uint16_t base_width = 0;
    StrokeFlags flags;
    SampleColumns samples;
};

// Shape geometry, spec 7.9.4. All values are coord units, rotation is in hundredths of a degree.
struct Ellipse {
    Coord cx = 0, cy = 0, rx = 0, ry = 0;  // rx and ry are positive
    std::uint16_t rotation = 0;            // 0 to 35999

    friend bool operator==(const Ellipse&, const Ellipse&) = default;
};

struct Rectangle {
    Coord cx = 0, cy = 0, width = 0, height = 0;  // width and height are positive
    std::uint16_t rotation = 0;                   // 0 to 35999

    friend bool operator==(const Rectangle&, const Rectangle&) = default;
};

struct Line {
    Coord x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    friend bool operator==(const Line&, const Line&) = default;
};

struct Triangle {
    Coord x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;

    friend bool operator==(const Triangle&, const Triangle&) = default;
};

using ShapeGeometry = std::variant<Ellipse, Rectangle, Line, Triangle>;

// A shape is ink like a stroke (spec 7.9.1) and shares its stroke_id space.
struct Shape {
    std::uint32_t stroke_id = 0;
    std::uint16_t layer = 0;
    std::optional<MsTime> created_at;
    Color color;
    std::uint16_t tool_type_raw = 0;      // 0 to 3
    ToolType tool = ToolType::Ballpoint;
    std::uint16_t base_width = 0;
    ShapeGeometry geometry;
};

// Strokes and shapes in the order of the chunks in strokes.bin (spec 3.4, 7.6).
using InkItem = std::variant<Stroke, Shape>;

}  // namespace onf
