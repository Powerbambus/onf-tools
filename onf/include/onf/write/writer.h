#pragma once

// The write side: a document in memory to an ONF 0.4.0 file (spec 11.5, Conforming Writer). The public interface
// uses no third-party types.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "onf/errors.h"
#include "onf/model/page.h"
#include "onf/model/types.h"
#include "onf/result.h"

namespace onf {

struct WritePage {
    PageMeta meta;             // size, background, text objects, asset placements, in painter's order
    std::vector<InkItem> ink;  // strokes and shapes, in painter's order within each layer (spec 7.6)
};

// An asset file to embed. The writer names it by the SHA-256 of `bytes` (spec 9.2.1); a placement refers to it by
// that hash and the same extension.
struct WriteAsset {
    std::string extension;  // webp, png, jpg or pdf
    std::vector<std::uint8_t> bytes;
};

struct WriteDocument {
    std::string title;
    std::optional<std::string> generator;
    UtcDateTime created_at;
    UtcDateTime modified_at;
    std::vector<WritePage> pages;  // in display order; the writer names the directories p00001 and up
    std::vector<WriteAsset> assets;
    std::optional<std::vector<std::uint8_t>> thumbnail;  // thumbnail.png, not checked
};

// The first rule of spec 11.5 that the document would break, or nothing. Written files are checked with it first.
// The message names the rule and the spec section.
std::optional<std::string> check_writable(const WriteDocument& document);

// Validates, then writes `path` (replacing it). manifest.json and meta.json are compressed, strokes.bin is stored
// (spec 4.5). On an error no partial file is left behind. Errors: WriteInvalidDocument, WriteFailed.
Result<void, FileError> write_onf(const WriteDocument& document, const std::filesystem::path& path);

}  // namespace onf
