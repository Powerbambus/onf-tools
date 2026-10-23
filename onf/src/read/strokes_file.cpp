#include "onf/read/strokes_file.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "onf/limits.h"
#include "onf/primitives/bytecursor.h"

namespace onf {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'O', 'N', 'F', 'S'};

Unexpected<PageError> fail(std::string_view page_id, ErrorCode code, std::string message, std::uint64_t offset) {
    return unexpected(PageError{std::string(page_id), code, std::move(message), offset});
}

}  // namespace

Result<StrokeFile, PageError> parse_strokes_file(std::span<const std::uint8_t> bytes, std::string_view page_id,
                                                 DiagnosticsSink* sink) {
    if (bytes.size() < limits::kStrokesFileHeaderSize) {
        return fail(page_id, ErrorCode::StrokesTooShort, "strokes.bin is shorter than its 8-byte header", 0);
    }

    ByteCursor cursor{bytes};
    const auto magic = cursor.read_bytes(kMagic.size()).value();
    if (!std::equal(magic.begin(), magic.end(), kMagic.begin())) {
        return fail(page_id, ErrorCode::StrokesBadMagic, "strokes.bin does not start with the magic bytes", 0);
    }

    const std::uint16_t version = cursor.read_u16().value();
    if (version == 0) {
        return fail(page_id, ErrorCode::StrokesVersionZero, "strokes.bin version is 0", 4);
    }
    if (version != kStrokesFormatVersion) {
        if (sink != nullptr) {
            sink->report({DiagnosticKind::StrokesVersionMismatch, std::string(page_id),
                          "strokes.bin version " + std::to_string(version) + " is not the supported " +
                              std::to_string(kStrokesFormatVersion)});
        }
        return fail(page_id, ErrorCode::StrokesVersionUnsupported, "strokes.bin version is not supported", 4);
    }
    static_cast<void>(cursor.skip(2));  // reserved bytes

    StrokeFile file;
    while (cursor.remaining() > 0) {
        const std::uint64_t frame_offset = cursor.position();
        if (cursor.remaining() < limits::kChunkFrameSize) {
            return fail(page_id, ErrorCode::ChunkHeaderTruncated, "incomplete chunk header at end of file", frame_offset);
        }

        const std::uint16_t type = cursor.read_u16().value();
        const std::uint32_t length = cursor.read_u32().value();
        if (length > cursor.remaining()) {
            return fail(page_id, ErrorCode::ChunkLengthExceedsData, "chunk length exceeds the remaining data",
                        frame_offset);
        }

        const std::uint64_t body_offset = cursor.position();
        const auto body = cursor.read_bytes(length).value();

        if (type == kChunkTypeEnd) break;

        if (type == kChunkTypeStroke) {
            if (body.size() < limits::kStrokeHeaderSize) {
                return fail(page_id, ErrorCode::StrokeHeaderTooShort,
                            "STROKE chunk is shorter than the 20-byte stroke header", frame_offset);
            }
        } else if (type == kChunkTypeShape) {
            if (body.size() < limits::kShapeHeaderSize) {
                return fail(page_id, ErrorCode::ShapeHeaderTooShort,
                            "SHAPE chunk is shorter than the 16-byte shape header", frame_offset);
            }
        } else {
            continue;  // unknown chunk types are skipped (spec 10.5.2)
        }
        file.ink_chunks.push_back({type, body, body_offset});
    }
    return file;
}

}  // namespace onf
