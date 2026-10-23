#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace onf_reader {

// RFC 4648 base64 with padding, for data: URIs in the SVG export.
std::string base64_encode(std::span<const std::uint8_t> data);

}  // namespace onf_reader
