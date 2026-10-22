#pragma once

#include <cstddef>
#include <cstdint>
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

// One vector per field. tilt_x, tilt_y and azimuth are empty when the stroke has no such fields.
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
    MsTime created_at = 0;
    Color color;
    std::uint16_t tool_type_raw = 0;
    ToolType tool = ToolType::Ballpoint;
    std::uint16_t base_width = 0;
    StrokeFlags flags;
    SampleColumns samples;
};

}  // namespace onf
