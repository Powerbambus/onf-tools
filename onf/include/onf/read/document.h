#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/container/zip_archive.h"
#include "onf/diagnostics.h"
#include "onf/errors.h"
#include "onf/model/manifest.h"
#include "onf/model/page.h"
#include "onf/read/asset_check.h"
#include "onf/read/json.h"
#include "onf/read/version_policy.h"
#include "onf/result.h"

namespace onf {

struct AssetOptions {
    // Compare the SHA-256 of the file with its name (spec 9.2.1: a reader SHOULD, and MAY skip it for large assets).
    bool verify_hash = true;
};

// The outcome of loading one asset placement. bytes is only filled when check.status is Ok.
struct AssetContent {
    AssetCheck check;
    std::vector<std::uint8_t> bytes;
};

// A file that was opened for reading. Not thread-safe.
class Document {
public:
    const Manifest& manifest() const noexcept { return manifest_; }

    // The pages in display order.
    const std::vector<PageEntry>& pages() const noexcept { return manifest_.pages; }

    // Reads meta.json and strokes.bin of one page. Any problem makes the page unreadable, no partial page is
    // returned and other pages are not affected. Problems are also reported to the sink as PageError events.
    Result<Page, PageError> page(std::string_view id, DiagnosticsSink* sink = nullptr) const;

    // Loads the file of an asset placement and checks it (spec 9.2.1 to 9.2.3). A missing file, an unsupported
    // extension and a corrupt file are not errors: the status says what happened, the matching event goes to the
    // sink, and the caller draws a placeholder. An error (FileError) is only returned if the archive cannot be read,
    // for example because a size limit of spec 4.7 is exceeded.
    Result<AssetContent, FileError> asset(const AssetRef& placement, std::string_view page_id, DiagnosticsSink* sink = nullptr,
                                          AssetOptions options = {}) const;

    // A copy of thumbnail.png, or nothing if the file has none. The bytes are not checked.
    Result<std::optional<std::vector<std::uint8_t>>, FileError> thumbnail_bytes() const;

private:
    friend class Inspection;
    Document(ZipArchive archive, Manifest manifest) : archive_(std::move(archive)), manifest_(std::move(manifest)) {}

    ZipArchive archive_;
    Manifest manifest_;
};

struct OpenDecision {
    // The host's answer to a version event (spec 10.2, 10.3): open although the version is not supported.
    bool allow_unsupported_version = false;
};

// Step one of opening: the container and the version of manifest.json have been checked.
class Inspection {
public:
    const Version& version() const noexcept { return version_; }
    VersionStatus status() const noexcept { return status_; }

    // Step two. Consumes the inspection. Errors: VersionRejected, manifest errors, PageDirectoryMissing.
    Result<Document, FileError> open(OpenDecision decision = {}) &&;

private:
    friend Result<Inspection, FileError> inspect(ByteSource source, DiagnosticsSink* sink);
    Inspection(ZipArchive archive, JsonValue manifest_json, Version version, VersionStatus status)
        : archive_(std::move(archive)), manifest_json_(std::move(manifest_json)), version_(version), status_(status) {}

    ZipArchive archive_;
    JsonValue manifest_json_;
    Version version_;
    VersionStatus status_;
};

// Opens the container, reads manifest.json and reports a version event to the sink if the version is not
// supported. Nothing else of the manifest is checked yet.
Result<Inspection, FileError> inspect(ByteSource source, DiagnosticsSink* sink = nullptr);

}  // namespace onf
