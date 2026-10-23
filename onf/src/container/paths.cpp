#include "onf/container/paths.h"

#include <cstdint>
#include <span>

#include "onf/primitives/utf8.h"

namespace onf {

namespace {

bool is_ascii_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool is_control(char c) {
    const auto byte = static_cast<std::uint8_t>(c);
    return byte < 0x20 || byte == 0x7F;
}

bool is_lower_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

}  // namespace

Result<void, ErrorCode> validate_entry_name(std::string_view name) {
    if (name.empty()) return unexpected(ErrorCode::UnsafePath);

    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());
    if (!is_valid_utf8(bytes)) return unexpected(ErrorCode::EntryNameInvalidUtf8);

    for (char c : name) {
        if (is_control(c) || c == '\\') return unexpected(ErrorCode::UnsafePath);
    }
    if (name.front() == '/') return unexpected(ErrorCode::UnsafePath);
    if (name.size() >= 2 && is_ascii_letter(name[0]) && name[1] == ':') return unexpected(ErrorCode::UnsafePath);

    if (name.back() == '/') name.remove_suffix(1);
    while (true) {
        const std::size_t slash = name.find('/');
        const std::string_view segment = name.substr(0, slash);
        if (segment.empty() || segment == "." || segment == "..") return unexpected(ErrorCode::UnsafePath);
        if (slash == std::string_view::npos) break;
        name.remove_prefix(slash + 1);
    }
    return {};
}

bool is_valid_page_id(std::string_view id) {
    if (id.size() != 6 || id[0] != 'p') return false;
    for (std::size_t i = 1; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
    }
    return true;
}

bool is_valid_asset_hash(std::string_view hash) {
    if (hash.size() != 64) return false;
    for (char c : hash) {
        if (!is_lower_hex(c)) return false;
    }
    return true;
}

bool is_supported_asset_extension(std::string_view extension) {
    return extension == "webp" || extension == "png" || extension == "jpg" || extension == "pdf";
}

}  // namespace onf
