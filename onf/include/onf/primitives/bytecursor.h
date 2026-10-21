#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "onf/result.h"

namespace onf {

struct ReadError {};

// Bounds-checked forward reader. Does not own the bytes; the buffer must outlive the cursor.
// A failed read leaves the position unchanged.
class ByteCursor {
public:
    explicit ByteCursor(std::span<const std::uint8_t> data) noexcept : data_(data) {}

    std::size_t position() const noexcept { return position_; }
    std::size_t remaining() const noexcept { return data_.size() - position_; }

    Result<std::uint8_t, ReadError> read_u8();
    Result<std::uint16_t, ReadError> read_u16();
    Result<std::uint32_t, ReadError> read_u32();
    Result<std::uint64_t, ReadError> read_u64();

    Result<std::span<const std::uint8_t>, ReadError> read_bytes(std::size_t count);
    Result<void, ReadError> skip(std::size_t count);

private:
    template <typename T>
    Result<T, ReadError> read_le();

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
};

}  // namespace onf
