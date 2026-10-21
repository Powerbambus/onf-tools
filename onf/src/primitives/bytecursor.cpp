#include "onf/primitives/bytecursor.h"

namespace onf {

template <typename T>
Result<T, ReadError> ByteCursor::read_le() {
    if (sizeof(T) > remaining()) return unexpected(ReadError{});

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= std::uint64_t{data_[position_ + i]} << (8 * i);
    }
    position_ += sizeof(T);
    return static_cast<T>(value);
}

Result<std::uint8_t, ReadError> ByteCursor::read_u8() { return read_le<std::uint8_t>(); }
Result<std::uint16_t, ReadError> ByteCursor::read_u16() { return read_le<std::uint16_t>(); }
Result<std::uint32_t, ReadError> ByteCursor::read_u32() { return read_le<std::uint32_t>(); }
Result<std::uint64_t, ReadError> ByteCursor::read_u64() { return read_le<std::uint64_t>(); }

Result<std::span<const std::uint8_t>, ReadError> ByteCursor::read_bytes(std::size_t count) {
    if (count > remaining()) return unexpected(ReadError{});

    const auto window = data_.subspan(position_, count);
    position_ += count;
    return window;
}

Result<void, ReadError> ByteCursor::skip(std::size_t count) {
    if (count > remaining()) return unexpected(ReadError{});

    position_ += count;
    return {};
}

}  // namespace onf
