#pragma once

#include <cstdint>
#include <vector>

namespace onf::test {

inline std::vector<std::uint8_t> encode_varint(std::uint64_t value) {
    std::vector<std::uint8_t> out;
    while (value > 0x7F) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
    return out;
}

inline std::uint64_t zigzag_encode(std::int64_t n) {
    return (static_cast<std::uint64_t>(n) << 1) ^ static_cast<std::uint64_t>(n >> 63);
}

}  // namespace onf::test
