#pragma once

#include <cstdint>

#include "onf/errors.h"
#include "onf/primitives/bytecursor.h"
#include "onf/result.h"

namespace onf {

// Unsigned LEB128 as restricted by spec 2.3.2 and A.2. max_value is kVarintMaxValue for zigzag
// fields and kTimestampMaxValue for the timestamp field. On error the cursor does not move.
Result<std::uint64_t, ErrorCode> decode_varint(ByteCursor& cursor, std::uint64_t max_value);

std::int64_t zigzag_decode(std::uint64_t value) noexcept;

}  // namespace onf
