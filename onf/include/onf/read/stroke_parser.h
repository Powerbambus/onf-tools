#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "onf/diagnostics.h"
#include "onf/errors.h"
#include "onf/model/stroke.h"
#include "onf/result.h"

namespace onf {

struct StrokeParseError {
    ErrorCode code;
    std::string message;
    std::uint64_t offset;  // position in the chunk body
};

// One STROKE chunk body: stroke header (spec 7.4.1), optional created_at and sample block (spec 7.5).
Result<Stroke, StrokeParseError> parse_stroke(std::span<const std::uint8_t> body);

// One SHAPE chunk body (spec 7.9.2 to 7.9.6). A shape type this version does not know is not an error: the
// result is empty and the chunk is skipped (spec 7.9.3).
Result<std::optional<Shape>, StrokeParseError> parse_shape(std::span<const std::uint8_t> body);

// A whole strokes.bin: strokes and shapes in file order. stroke_id is unique across both. Any error makes the
// page unreadable, no partial result is returned.
Result<std::vector<InkItem>, PageError> parse_ink(std::span<const std::uint8_t> bytes, std::string_view page_id,
                                                  DiagnosticsSink* sink = nullptr);

}  // namespace onf
