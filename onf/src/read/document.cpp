#include "onf/read/document.h"

#include <algorithm>
#include <string>
#include <utility>

#include "onf/limits.h"
#include "onf/primitives/sha256.h"
#include "onf/read/manifest.h"
#include "onf/read/page_meta.h"
#include "onf/read/stroke_parser.h"

namespace onf {

namespace {

PageError page_error(std::string_view id, ErrorCode code, std::string message, std::optional<std::uint64_t> offset = std::nullopt) {
    return PageError{std::string(id), code, std::move(message), offset};
}

Result<Page, PageError> read_page(const ZipArchive& archive, const Manifest& manifest, std::string_view id,
                                  DiagnosticsSink* sink) {
    const bool listed = std::any_of(manifest.pages.begin(), manifest.pages.end(),
                                    [&](const PageEntry& entry) { return entry.id == id; });
    if (!listed) return unexpected(page_error(id, ErrorCode::PageDirectoryMissing, "the manifest has no such page"));

    auto meta = read_page_meta(archive, id);
    if (!meta) return unexpected(std::move(meta).error());

    const std::string name = "pages/" + std::string(id) + "/strokes.bin";
    const auto index = archive.find(name);
    if (!index) return unexpected(page_error(id, ErrorCode::PageFileMissing, name + " is missing"));

    auto bytes = archive.read(*index);
    if (!bytes) return unexpected(page_error(id, bytes.error().code, bytes.error().message));

    auto ink = parse_ink(bytes.value(), id, sink);
    if (!ink) return unexpected(std::move(ink).error());

    return Page{std::string(id), std::move(meta).value(), std::move(ink).value()};
}

}  // namespace

Result<Page, PageError> Document::page(std::string_view id, DiagnosticsSink* sink) const {
    auto result = read_page(archive_, manifest_, id, sink);
    if (!result && sink != nullptr) {
        sink->report({DiagnosticKind::PageError, std::string(id), result.error().message});
    }
    return result;
}

Result<AssetContent, FileError> Document::asset(const AssetRef& placement, std::string_view page_id, DiagnosticsSink* sink,
                                                AssetOptions options) const {
    const auto report = [&](DiagnosticKind kind, const std::string& detail) {
        if (sink != nullptr) sink->report({kind, std::string(page_id), "asset " + placement.hash + ": " + detail});
    };
    AssetContent content;

    // The extension is part of the path, so it is only used after it is known to be one of spec 9.2.2 (spec 4.6.1).
    if (!is_supported_asset_extension(placement.extension)) {
        content.check = {AssetStatus::UnsupportedFormat, "the extension \"" + placement.extension + "\" is not supported", std::nullopt, std::nullopt};
        report(DiagnosticKind::UnsupportedAssetFormat, content.check.detail);
        return content;
    }

    const std::string name = "assets/" + placement.hash + "." + placement.extension;
    const auto index = archive_.find(name);
    if (!index) {
        content.check = {AssetStatus::Missing, name + " is not in the archive", std::nullopt, std::nullopt};
        report(DiagnosticKind::MissingAsset, content.check.detail);
        return content;
    }

    auto bytes = archive_.read(*index);
    if (!bytes) return unexpected(std::move(bytes).error());

    if (options.verify_hash && sha256_hex(bytes.value()) != placement.hash) {
        content.check = {AssetStatus::Corrupt, "the SHA-256 of the file does not match its name", std::nullopt, std::nullopt};
        report(DiagnosticKind::CorruptAsset, content.check.detail);
        return content;
    }

    content.check = check_asset_content(placement.extension, bytes.value());
    if (content.check.status != AssetStatus::Ok) {
        report(DiagnosticKind::CorruptAsset, content.check.detail);
        return content;
    }

    // Spec 9.4.5: the crop region has to lie completely inside the image. Only now are the pixel dimensions known.
    // A region that does not is a corrupt asset, not a malformed page.
    if (placement.crop && content.check.width && content.check.height) {
        const Crop& crop = *placement.crop;
        const bool inside = static_cast<std::int64_t>(crop.x) + crop.width <= *content.check.width &&
                            static_cast<std::int64_t>(crop.y) + crop.height <= *content.check.height;
        if (!inside) {
            const auto dimensions = content.check;
            content.check = {AssetStatus::Corrupt, "the crop region does not lie inside the image", dimensions.width, dimensions.height};
            report(DiagnosticKind::CorruptAsset, content.check.detail);
            return content;
        }
    }
    content.bytes = std::move(bytes).value();
    return content;
}

Result<std::optional<std::vector<std::uint8_t>>, FileError> Document::thumbnail_bytes() const {
    constexpr const char* kName = "thumbnail.png";
    const auto index = archive_.find(kName);
    if (!index) return std::optional<std::vector<std::uint8_t>>{};

    if (archive_.entries()[*index].size > limits::kMaxThumbnailSize) {
        return unexpected(FileError{ErrorCode::ThumbnailTooLarge, "thumbnail.png is larger than 16 MiB", kName});
    }
    auto bytes = archive_.read(*index);
    if (!bytes) return unexpected(std::move(bytes).error());
    return std::optional<std::vector<std::uint8_t>>(std::move(bytes).value());
}

Result<Document, FileError> Inspection::open(OpenDecision decision) && {
    auto allowed = check_version_policy(version_, status_, decision.allow_unsupported_version);
    if (!allowed) return unexpected(std::move(allowed).error());

    auto manifest = parse_manifest(manifest_json_);
    if (!manifest) return unexpected(std::move(manifest).error());

    auto directories = check_page_directories(archive_, manifest.value());
    if (!directories) return unexpected(std::move(directories).error());

    return Document(std::move(archive_), std::move(manifest).value());
}

Result<Inspection, FileError> inspect(ByteSource source, DiagnosticsSink* sink) {
    auto archive = ZipArchive::open(std::move(source));
    if (!archive) return unexpected(std::move(archive).error());

    auto json = read_manifest_json(archive.value());
    if (!json) return unexpected(std::move(json).error());

    auto version = parse_manifest_version(json.value());
    if (!version) return unexpected(std::move(version).error());

    const VersionStatus status = classify_version(version.value());
    report_version_event(version.value(), status, sink);
    return Inspection(std::move(archive).value(), std::move(json).value(), version.value(), status);
}

}  // namespace onf
