#include "onf/read/asset_check.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace onf {

namespace {

AssetCheck corrupt(std::string detail) { return AssetCheck{AssetStatus::Corrupt, std::move(detail), std::nullopt, std::nullopt}; }

AssetCheck with_size(std::uint64_t width, std::uint64_t height) {
    if (width == 0 || height == 0) return corrupt("the image has a width or height of 0");
    if (width * height > kMaxDecodedPixels) return corrupt("the image has more than 100000000 pixels");
    AssetCheck ok;
    ok.width = static_cast<std::uint32_t>(width);
    ok.height = static_cast<std::uint32_t>(height);
    return ok;
}

bool starts_with(std::span<const std::uint8_t> bytes, std::span<const std::uint8_t> prefix) noexcept {
    return bytes.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), bytes.begin());
}

std::uint32_t be32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return (static_cast<std::uint32_t>(bytes[at]) << 24) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
}

std::uint32_t be16(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return (static_cast<std::uint32_t>(bytes[at]) << 8) | bytes[at + 1];
}

std::uint32_t le16(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return bytes[at] | (static_cast<std::uint32_t>(bytes[at + 1]) << 8);
}

std::uint32_t le24(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return bytes[at] | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) | (static_cast<std::uint32_t>(bytes[at + 2]) << 16);
}

// PNG: signature, then the IHDR chunk with width and height as big-endian u32.
AssetCheck check_png(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!starts_with(bytes, kSignature)) return corrupt("the file does not start with the PNG signature");
    if (bytes.size() < 24 || be32(bytes, 8) != 13 || bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R') {
        return corrupt("the PNG file has no IHDR chunk at the start");
    }
    return with_size(be32(bytes, 16), be32(bytes, 20));
}

// JPEG: FF D8 FF, then segments. The first start-of-frame segment holds height and width (big-endian u16).
AssetCheck check_jpeg(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t, 3> kSignature = {0xFF, 0xD8, 0xFF};
    if (!starts_with(bytes, kSignature)) return corrupt("the file does not start with the JPEG signature");

    std::size_t at = 2;
    while (at + 4 <= bytes.size()) {
        if (bytes[at] != 0xFF) return corrupt("the JPEG segments are malformed");
        const std::uint8_t marker = bytes[at + 1];
        if (marker == 0xFF) { ++at; continue; }  // fill byte
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) { at += 2; continue; }  // markers without a length
        const std::size_t length = be16(bytes, at + 2);
        if (length < 2) return corrupt("a JPEG segment has an invalid length");

        const bool start_of_frame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (start_of_frame) {
            if (length < 8 || at + 9 > bytes.size()) return corrupt("the JPEG frame header is truncated");
            return with_size(be16(bytes, at + 7), be16(bytes, at + 5));
        }
        if (marker == 0xD9 || marker == 0xDA) break;  // end of image / start of scan before any frame header
        at += 2 + length;
    }
    return corrupt("the JPEG file has no frame header");
}

// WebP: RIFF container with a VP8 (lossy), VP8L (lossless) or VP8X (extended) chunk first.
AssetCheck check_webp(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 20 || !std::equal(bytes.begin(), bytes.begin() + 4, "RIFF") ||
        !std::equal(bytes.begin() + 8, bytes.begin() + 12, "WEBP")) {
        return corrupt("the file does not start with a RIFF WebP header");
    }
    const auto fourcc = [&](const char* name) { return std::equal(bytes.begin() + 12, bytes.begin() + 16, name); };
    const std::size_t data = 20;  // first byte of the chunk data

    if (fourcc("VP8 ")) {
        if (bytes.size() < data + 10 || bytes[data + 3] != 0x9D || bytes[data + 4] != 0x01 || bytes[data + 5] != 0x2A) {
            return corrupt("the VP8 frame header is invalid");
        }
        return with_size(le16(bytes, data + 6) & 0x3FFF, le16(bytes, data + 8) & 0x3FFF);
    }
    if (fourcc("VP8L")) {
        if (bytes.size() < data + 5 || bytes[data] != 0x2F) return corrupt("the VP8L header is invalid");
        const std::uint32_t bits = bytes[data + 1] | (static_cast<std::uint32_t>(bytes[data + 2]) << 8) |
                                   (static_cast<std::uint32_t>(bytes[data + 3]) << 16) |
                                   (static_cast<std::uint32_t>(bytes[data + 4]) << 24);
        return with_size((bits & 0x3FFFu) + 1, ((bits >> 14) & 0x3FFFu) + 1);
    }
    if (fourcc("VP8X")) {
        if (bytes.size() < data + 10) return corrupt("the VP8X header is truncated");
        return with_size(static_cast<std::uint64_t>(le24(bytes, data + 4)) + 1, static_cast<std::uint64_t>(le24(bytes, data + 7)) + 1);
    }
    return corrupt("the WebP file has no VP8, VP8L or VP8X chunk at the start");
}

}  // namespace

AssetCheck check_asset_content(std::string_view extension, std::span<const std::uint8_t> bytes) {
    if (extension == "png") return check_png(bytes);
    if (extension == "jpg") return check_jpeg(bytes);
    if (extension == "webp") return check_webp(bytes);
    if (extension == "pdf") {
        constexpr std::array<std::uint8_t, 5> kMagic = {'%', 'P', 'D', 'F', '-'};
        if (!starts_with(bytes, kMagic)) return corrupt("the file does not start with %PDF-");
        return AssetCheck{};
    }
    return AssetCheck{AssetStatus::UnsupportedFormat, "the extension is not supported", std::nullopt, std::nullopt};
}

}  // namespace onf
