#include "onf/primitives/utf8.h"

namespace onf {

std::size_t utf8_sequence_length(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.empty()) return 0;

    const std::uint8_t lead = bytes[0];
    if (lead < 0x80) return 1;

    std::size_t length;
    std::uint8_t second_min = 0x80, second_max = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        if (lead == 0xE0) second_min = 0xA0;
        if (lead == 0xED) second_max = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        if (lead == 0xF0) second_min = 0x90;
        if (lead == 0xF4) second_max = 0x8F;
    } else {
        return 0;
    }

    if (bytes.size() < length) return 0;
    if (bytes[1] < second_min || bytes[1] > second_max) return 0;
    for (std::size_t i = 2; i < length; ++i) {
        if (bytes[i] < 0x80 || bytes[i] > 0xBF) return 0;
    }
    return length;
}

bool is_valid_utf8(std::span<const std::uint8_t> bytes) noexcept {
    while (!bytes.empty()) {
        const std::size_t length = utf8_sequence_length(bytes);
        if (length == 0) return false;
        bytes = bytes.subspan(length);
    }
    return true;
}

}  // namespace onf
