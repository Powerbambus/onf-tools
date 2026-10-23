// A property of reader and writer together: whatever strokes.bin the reader accepts, writing the result and reading it
// again gives the same strokes and shapes. A difference aborts, so the fuzzer reports the input.
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

#include "onf/read/stroke_parser.h"
#include "onf/write/strokes.h"

namespace {

bool same(const onf::Stroke& a, const onf::Stroke& b) {
    const onf::SampleColumns& x = a.samples;
    const onf::SampleColumns& y = b.samples;
    return a.stroke_id == b.stroke_id && a.layer == b.layer && a.created_at == b.created_at && a.color == b.color && a.tool_type_raw == b.tool_type_raw &&
           a.base_width == b.base_width && (a.flags.raw() & 0x7F) == (b.flags.raw() & 0x7F) && x.x == y.x && x.y == y.y && x.pressure == y.pressure &&
           x.timestamp == y.timestamp && x.tilt_x == y.tilt_x && x.tilt_y == y.tilt_y && x.azimuth == y.azimuth;
}

bool same(const onf::Shape& a, const onf::Shape& b) {
    return a.stroke_id == b.stroke_id && a.layer == b.layer && a.created_at == b.created_at && a.color == b.color && a.tool_type_raw == b.tool_type_raw &&
           a.base_width == b.base_width && a.geometry.index() == b.geometry.index() &&
           a.geometry == b.geometry;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto first = onf::parse_ink(std::span<const std::uint8_t>(data, size), "p00001");
    if (!first) return 0;

    const std::vector<std::uint8_t> encoded = onf::encode_strokes_file(first.value());
    auto second = onf::parse_ink(encoded, "p00001");
    if (!second || second.value().size() != first.value().size()) std::abort();

    for (std::size_t i = 0; i < first.value().size(); ++i) {
        const onf::InkItem& a = first.value()[i];
        const onf::InkItem& b = second.value()[i];
        if (a.index() != b.index()) std::abort();
        const bool equal = a.index() == 0 ? same(std::get<onf::Stroke>(a), std::get<onf::Stroke>(b))
                                           : same(std::get<onf::Shape>(a), std::get<onf::Shape>(b));
        if (!equal) std::abort();
    }
    return 0;
}
