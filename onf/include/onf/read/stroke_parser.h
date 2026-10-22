#pragma once

#include <cstdint>
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

// One STROKE chunk body: stroke header (spec 7.4.1) and sample block (spec 7.5).
Result<Stroke, StrokeParseError> parse_stroke(std::span<const std::uint8_t> body);

// A whole strokes.bin. Any error makes the page unreadable, no partial result is returned.
Result<std::vector<Stroke>, PageError> parse_strokes(std::span<const std::uint8_t> bytes, std::string_view page_id,
                                                     DiagnosticsSink* sink = nullptr);

}  // namespace onf
