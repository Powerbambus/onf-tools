#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "onf/diagnostics.h"
#include "onf/errors.h"
#include "onf/result.h"

namespace onf {

inline constexpr std::uint16_t kChunkTypeStroke = 0x0001;
inline constexpr std::uint16_t kChunkTypeEnd = 0x0002;
inline constexpr std::uint16_t kStrokesFormatVersion = 1;

struct StrokeChunk {
    std::span<const std::uint8_t> body;  // points into the parsed buffer
    std::uint64_t offset;                // position of the body in the file
};

struct StrokeFile {
    std::vector<StrokeChunk> stroke_chunks;
};

// Header and chunk framing of strokes.bin (spec 7.2, 7.3, 7.3.2). The stroke records are not decoded.
// A strokes version above kStrokesFormatVersion is reported to the sink as StrokesVersionMismatch.
Result<StrokeFile, PageError> parse_strokes_file(std::span<const std::uint8_t> bytes, std::string_view page_id,
                                                 DiagnosticsSink* sink = nullptr);

}  // namespace onf
