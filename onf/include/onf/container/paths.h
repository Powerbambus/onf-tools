#pragma once

#include <string_view>

#include "onf/errors.h"
#include "onf/result.h"

namespace onf {

// Archive entry names (spec 4.6, strict): valid UTF-8, no NUL or control characters, no backslash, not absolute,
// no drive letter ("C:"), no empty, "." or ".." segment. A single trailing slash (directory entry) is allowed.
// Errors: EntryNameInvalidUtf8, UnsafePath.
Result<void, ErrorCode> validate_entry_name(std::string_view name);

// Identifiers that are used to build paths (spec 4.6.1).
bool is_valid_page_id(std::string_view id);            // ^p[0-9]{5}$
bool is_valid_asset_hash(std::string_view hash);       // ^[0-9a-f]{64}$
bool is_supported_asset_extension(std::string_view extension);  // webp, png, jpg, pdf (spec 9.2.2)

}  // namespace onf
