// strokes.bin on its own: header, chunk framing, strokes, shapes. Much faster than going through a ZIP archive.
#include <cstdint>
#include <span>

#include "onf/read/stroke_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static_cast<void>(onf::parse_ink(std::span<const std::uint8_t>(data, size), "p00001"));
    return 0;
}
