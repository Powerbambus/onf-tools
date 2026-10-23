#pragma once

#include <cstdint>
#include <vector>

#include "onf/errors.h"
#include "onf/primitives/bytecursor.h"
#include "onf/result.h"

namespace onf {

// Unsigned LEB128 as restricted by spec 2.3.2 and A.2. max_value is kVarintMaxValue for zigzag
// fields and kTimestampMaxValue for the timestamp field. On error the cursor does not move.
Result<std::uint64_t, ErrorCode> decode_varint(ByteCursor& cursor, std::uint64_t max_value);

std::int64_t zigzag_decode(std::uint64_t value) noexcept;

// The inverse direction, for writers (spec 7.5.3, A.1, A.2). The value is encoded in the shortest form, which is the
// only canonical one. Appends 1 to 5 bytes for values up to 2^33 - 1.
void append_varint(std::vector<std::uint8_t>& out, std::uint64_t value);
std::uint64_t zigzag_to_unsigned(std::int64_t value) noexcept;

}  // namespace onf
