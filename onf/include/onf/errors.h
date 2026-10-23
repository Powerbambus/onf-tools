#pragma once

// Typed errors for malformed input (spec 12.3). Numeric values are stable and never reused.
// The scope (file or page) is given by FileError / PageError, not by the code.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace onf {

// X(name, number, text name, spec section)
#define ONF_ERROR_CODES(X)                                                             \
    /* 1xx: container */                                     \
    X(ZipOpenFailed,            101, "zip_open_failed",            "4.1")              \
    X(ZipEncryptedEntry,        102, "zip_encrypted_entry",        "4.1")              \
    X(ZipMultiDisk,             103, "zip_multi_disk",             "4.1")              \
    X(ZipDuplicateEntry,        104, "zip_duplicate_entry",        "4.1")              \
    X(ZipSymlinkEntry,          105, "zip_symlink_entry",          "4.1")              \
    X(ZipUnsupportedMethod,     106, "zip_unsupported_method",     "4.5")              \
    X(EntryNameInvalidUtf8,     107, "entry_name_invalid_utf8",    "4.6")              \
    X(UnsafePath,               108, "unsafe_path",                "4.6")              \
    X(InvalidIdentifier,        109, "invalid_identifier",         "4.6.1")            \
    X(EntrySizeLimitExceeded,   110, "entry_size_limit_exceeded",  "4.7")              \
    X(TotalSizeLimitExceeded,   111, "total_size_limit_exceeded",  "4.7")              \
    X(CompressionRatioExceeded, 112, "compression_ratio_exceeded", "4.7")              \
    X(ZipReadFailed,            113, "zip_read_failed",            "4.1")              \
    /* 2xx: JSON */                                                                    \
    X(JsonSyntax,               201, "json_syntax",                "2.3.5")            \
    X(JsonInvalidUtf8,          202, "json_invalid_utf8",          "2.3.5")            \
    X(JsonBom,                  203, "json_bom",                   "2.3.5")            \
    X(JsonDuplicateKey,         204, "json_duplicate_key",         "2.3.5")            \
    X(JsonNestingTooDeep,       205, "json_nesting_too_deep",      "12.6")             \
    X(JsonStringTooLong,        206, "json_string_too_long",       "12.6")             \
    X(JsonNonIntegerNumber,     207, "json_non_integer_number",    "2.3.5")            \
    X(JsonIntegerOutOfRange,    208, "json_integer_out_of_range",  "2.3.5")            \
    X(JsonUnpairedSurrogate,    209, "json_unpaired_surrogate",    "2.3.5")            \
    /* 3xx: manifest, meta.json */                                         \
    X(ManifestMissing,          301, "manifest_missing",           "6.1")              \
    X(ManifestInvalidField,     302, "manifest_invalid_field",     "6.1.2")            \
    X(ManifestPagesInvalid,     303, "manifest_pages_invalid",     "6.1.2")            \
    X(PageDirectoryMissing,     304, "page_directory_missing",     "6.1.2")            \
    X(PageFileMissing,          305, "page_file_missing",          "4.2")              \
    X(MetaInvalidField,         306, "meta_invalid_field",         "6.2.2")            \
    X(BackgroundInvalidField,   307, "background_invalid_field",   "6.3.1")            \
    X(VersionRejected,          308, "version_rejected",           "10.3")             \
    X(ManifestTooLarge,         309, "manifest_too_large",         "12.6")             \
    X(ThumbnailTooLarge,        310, "thumbnail_too_large",        "4.3")              \
    X(LayerInvalid,             311, "layer_invalid",              "5.6")              \
    X(SpansInvalid,             312, "spans_invalid",              "8.3.1")            \
    X(DuplicateObjectId,        313, "duplicate_object_id",        "8.3")              \
    /* 4xx: strokes.bin */                                                             \
    X(StrokesTooShort,          401, "strokes_too_short",          "7.2")              \
    X(StrokesBadMagic,          402, "strokes_bad_magic",          "7.2")              \
    X(StrokesVersionZero,       403, "strokes_version_zero",       "7.2")              \
    X(StrokesVersionUnsupported, 404, "strokes_version_unsupported", "7.2")           \
    X(ChunkHeaderTruncated,     405, "chunk_header_truncated",     "7.3.2")            \
    X(ChunkLengthExceedsData,   406, "chunk_length_exceeds_data",  "7.3.2")            \
    X(StrokeHeaderTooShort,     407, "stroke_header_too_short",    "7.3.2")            \
    X(StrokeSampleCountZero,    408, "stroke_sample_count_zero",   "7.3.2")            \
    X(StrokeBaseWidthZero,      409, "stroke_base_width_zero",     "7.3.2")            \
    X(StrokeSamplesTruncated,   410, "stroke_samples_truncated",   "7.3.2")            \
    X(StrokeIdDuplicate,        411, "stroke_id_duplicate",        "7.3.2")            \
    X(SampleValueOutOfRange,    412, "sample_value_out_of_range",  "7.5.2")            \
    X(InkLayerZero,             413, "ink_layer_zero",             "7.3.2")            \
    X(ShapeHeaderTooShort,      414, "shape_header_too_short",     "7.3.2")            \
    X(ShapeParametersTruncated, 415, "shape_parameters_truncated", "7.3.2")            \
    X(ShapeToolTypeInvalid,     416, "shape_tool_type_invalid",    "7.9.2")            \
    X(ShapeParameterInvalid,    417, "shape_parameter_invalid",    "7.9.4")            \
    /* 5xx: varint */                                                                  \
    X(VarintTruncated,          501, "varint_truncated",           "2.3.2")            \
    X(VarintTooLong,            502, "varint_too_long",            "2.3.2")            \
    X(VarintOutOfRange,         503, "varint_out_of_range",        "2.3.2")            \
    X(VarintNotCanonical,       504, "varint_not_canonical",       "2.3.2")                \
    /* 6xx: writing */                                                                 \
    X(WriteInvalidDocument,     601, "write_invalid_document",     "11.5")             \
    X(WriteFailed,              602, "write_failed",               "4.1")

enum class ErrorCode : std::uint16_t {
#define ONF_X(name, value, text, ref) name = value,
    ONF_ERROR_CODES(ONF_X)
#undef ONF_X
};

struct ErrorInfo {
    ErrorCode code;
    std::string_view name;
    std::string_view spec_ref;
};

std::span<const ErrorInfo> error_table() noexcept;

// "unknown" and "" for a value that is not in the table.
std::string_view error_name(ErrorCode code) noexcept;
std::string_view error_spec_ref(ErrorCode code) noexcept;

// Container or manifest.json malformed: the whole file is rejected (spec 12.3.1).
struct FileError {
    ErrorCode code;
    std::string message;
    std::string entry;  // empty if none
};

// meta.json or strokes.bin of one page malformed: only this page is unreadable (spec 12.3.1).
struct PageError {
    std::string page_id;
    ErrorCode code;
    std::string message;
    std::optional<std::uint64_t> offset;
};

}  // namespace onf
