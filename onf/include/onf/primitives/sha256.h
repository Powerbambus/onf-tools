#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace onf {

// SHA-256 as defined in FIPS 180-4. Asset files are named by this hash (spec 9.2.1).
std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) noexcept;

// 64 lowercase hexadecimal digits.
std::string sha256_hex(std::span<const std::uint8_t> data);

}  // namespace onf
