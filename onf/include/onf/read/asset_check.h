#pragma once

// Content checks of an asset file (spec 9.2.2, 9.2.3). Nothing is decoded: the checks read the header only,
// before any decoder sees the data. None of the outcomes is a malformed-file error, the page stays readable.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "onf/container/paths.h"

namespace onf {

enum class AssetStatus {
    Ok,
    Missing,            // the file is not in the archive (spec 9.3.2)
    UnsupportedFormat,  // the extension is not one of spec 9.2.2
    Corrupt,            // wrong magic, hash mismatch, unreadable header or too large (spec 9.2.1, 9.2.3)
};

struct AssetCheck {
    AssetStatus status = AssetStatus::Ok;
    std::string detail;                      // empty for Ok
    std::optional<std::uint32_t> width;      // pixels, set for images with a readable header
    std::optional<std::uint32_t> height;
};

// Spec 9.2.3: the leading bytes must match the extension, the declared pixel count of an image must not exceed
// 100000000. For pdf only the magic `%PDF-` is checked, the rest needs a PDF parser (page count, encryption).
// An unsupported extension gives UnsupportedFormat.
AssetCheck check_asset_content(std::string_view extension, std::span<const std::uint8_t> bytes);

inline constexpr std::uint64_t kMaxDecodedPixels = 100000000ull;

}  // namespace onf
