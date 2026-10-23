#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace onf {

// Length of the well-formed UTF-8 sequence at the start of `bytes`, or 0 if it is empty or not well-formed.
// Overlong forms, encoded surrogates and code points above U+10FFFF are not well-formed.
std::size_t utf8_sequence_length(std::span<const std::uint8_t> bytes) noexcept;

bool is_valid_utf8(std::span<const std::uint8_t> bytes) noexcept;

// Number of code points of a string that is valid UTF-8 (spec 12.6 counts length in code points).
std::size_t count_code_points(std::string_view text) noexcept;

}  // namespace onf
