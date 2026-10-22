#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace onf::test {

using Bytes = std::vector<std::uint8_t>;

inline void append_u16(Bytes& out, std::uint16_t v) {
    for (unsigned i = 0; i < 2; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void append_u32(Bytes& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void append(Bytes& out, std::span<const std::uint8_t> more) { out.insert(out.end(), more.begin(), more.end()); }

inline Bytes file_header(std::uint16_t version = 1, std::array<std::uint8_t, 4> magic = {0x4F, 0x4E, 0x46, 0x53},
                         std::array<std::uint8_t, 2> reserved = {0, 0}) {
    Bytes out(magic.begin(), magic.end());
    append_u16(out, version);
    out.insert(out.end(), reserved.begin(), reserved.end());
    return out;
}

inline Bytes chunk_with_length(std::uint16_t type, std::uint32_t declared_length, std::span<const std::uint8_t> body) {
    Bytes out;
    append_u16(out, type);
    append_u32(out, declared_length);
    append(out, body);
    return out;
}

inline Bytes chunk(std::uint16_t type, std::span<const std::uint8_t> body = {}) {
    return chunk_with_length(type, static_cast<std::uint32_t>(body.size()), body);
}

inline Bytes filler(std::size_t size, std::uint8_t value = 0xEE) { return Bytes(size, value); }

inline Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& part : parts) append(out, part);
    return out;
}

}  // namespace onf::test
