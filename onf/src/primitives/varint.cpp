#include "onf/primitives/varint.h"

#include "onf/limits.h"

namespace onf {

Result<std::uint64_t, ErrorCode> decode_varint(ByteCursor& cursor, std::uint64_t max_value) {
    ByteCursor probe = cursor;
    std::uint64_t result = 0;

    for (unsigned i = 0; i < limits::kVarintMaxBytes; ++i) {
        const auto next = probe.read_u8();
        if (!next) return unexpected(ErrorCode::VarintTruncated);

        const std::uint8_t byte = next.value();
        result |= static_cast<std::uint64_t>(byte & 0x7F) << (7 * i);

        if ((byte & 0x80) == 0) {
            if (i > 0 && byte == 0) return unexpected(ErrorCode::VarintNotCanonical);
            if (result > max_value) return unexpected(ErrorCode::VarintOutOfRange);
            cursor = probe;
            return result;
        }
    }
    return unexpected(ErrorCode::VarintTooLong);
}

std::int64_t zigzag_decode(std::uint64_t value) noexcept {
    return static_cast<std::int64_t>((value >> 1) ^ (0 - (value & 1)));
}

void append_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value > 0x7F) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

std::uint64_t zigzag_to_unsigned(std::int64_t value) noexcept {
    return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}

}  // namespace onf
