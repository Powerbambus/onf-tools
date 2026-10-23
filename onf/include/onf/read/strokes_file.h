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
inline constexpr std::uint16_t kChunkTypeShape = 0x0003;
inline constexpr std::uint16_t kStrokesFormatVersion = 2;

// A STROKE or SHAPE chunk, both are ink (spec 3.4).
struct InkChunk {
    std::uint16_t type;                  // kChunkTypeStroke or kChunkTypeShape
    std::span<const std::uint8_t> body;  // points into the parsed buffer
    std::uint64_t offset;                // position of the body in the file
};

struct StrokeFile {
    std::vector<InkChunk> ink_chunks;  // in file order
};

// Header and chunk framing of strokes.bin (spec 7.2, 7.3, 7.3.2). The records are not decoded.
// A strokes version other than kStrokesFormatVersion is reported to the sink as StrokesVersionMismatch.
Result<StrokeFile, PageError> parse_strokes_file(std::span<const std::uint8_t> bytes, std::string_view page_id,
                                                 DiagnosticsSink* sink = nullptr);

}  // namespace onf
