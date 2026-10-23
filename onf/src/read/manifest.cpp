#include "onf/read/manifest.h"

#include <string>
#include <unordered_set>
#include <utility>

#include "onf/container/paths.h"
#include "onf/limits.h"
#include "onf/read/values.h"

namespace onf {

namespace {

constexpr const char* kManifestName = "manifest.json";

Unexpected<FileError> fail(ErrorCode code, std::string message) {
    return unexpected(FileError{code, std::move(message), kManifestName});
}

Result<const std::string*, FileError> required_string(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.find(key);
    if (value == nullptr) return fail(ErrorCode::ManifestInvalidField, "missing field \"" + std::string(key) + "\"");
    const std::string* text = value->get_string();
    if (text == nullptr) return fail(ErrorCode::ManifestInvalidField, "field \"" + std::string(key) + "\" must be a string");
    return text;
}

Result<UtcDateTime, FileError> required_datetime(const JsonValue& object, std::string_view key) {
    auto text = required_string(object, key);
    if (!text) return unexpected(std::move(text).error());
    const auto parsed = parse_utc_datetime(*text.value());
    if (!parsed) {
        return fail(ErrorCode::ManifestInvalidField,
                    "field \"" + std::string(key) + "\" is not a UTC datetime like 2026-06-14T10:32:00.000Z");
    }
    return *parsed;
}

}  // namespace

Result<JsonValue, FileError> read_manifest_json(const ZipArchive& archive) {
    const auto index = archive.find(kManifestName);
    if (!index) return fail(ErrorCode::ManifestMissing, "the archive has no manifest.json");

    if (archive.entries()[*index].size > limits::kMaxManifestSize) {
        return fail(ErrorCode::ManifestTooLarge, "manifest.json is larger than 16 MiB");
    }

    auto bytes = archive.read(*index);
    if (!bytes) return unexpected(std::move(bytes).error());

    auto json = parse_json(bytes.value());
    if (!json) {
        const JsonError& e = json.error();
        return fail(e.code, e.message + " (at byte " + std::to_string(e.offset) + ")");
    }
    return std::move(json).value();
}

Result<Version, FileError> parse_manifest_version(const JsonValue& root) {
    if (root.get_object() == nullptr) return fail(ErrorCode::ManifestInvalidField, "manifest.json must contain a JSON object");

    auto text = required_string(root, "onf_version");
    if (!text) return unexpected(std::move(text).error());
    const auto version = parse_version(*text.value());
    if (!version) return fail(ErrorCode::ManifestInvalidField, "field \"onf_version\" is not a version like 0.3.0");
    return *version;
}

Result<Manifest, FileError> parse_manifest(const JsonValue& root) {
    auto version = parse_manifest_version(root);
    if (!version) return unexpected(std::move(version).error());

    Manifest manifest;
    manifest.onf_version = version.value();

    if (const JsonValue* generator = root.find("generator")) {
        const std::string* text = generator->get_string();
        if (text == nullptr) return fail(ErrorCode::ManifestInvalidField, "field \"generator\" must be a string");
        manifest.generator = *text;
    }

    auto title = required_string(root, "title");
    if (!title) return unexpected(std::move(title).error());
    manifest.title = *title.value();

    auto created = required_datetime(root, "created_at");
    if (!created) return unexpected(std::move(created).error());
    manifest.created_at = created.value();

    auto modified = required_datetime(root, "modified_at");
    if (!modified) return unexpected(std::move(modified).error());
    manifest.modified_at = modified.value();

    const JsonValue* pages_value = root.find("pages");
    if (pages_value == nullptr) return fail(ErrorCode::ManifestInvalidField, "missing field \"pages\"");
    const JsonValue::Array* pages = pages_value->get_array();
    if (pages == nullptr) return fail(ErrorCode::ManifestInvalidField, "field \"pages\" must be an array");
    if (pages->empty()) return fail(ErrorCode::ManifestPagesInvalid, "pages must contain at least one entry");
    if (pages->size() > limits::kMaxPages) return fail(ErrorCode::ManifestPagesInvalid, "pages contains more than 99999 entries");

    const std::size_t count = pages->size();
    std::vector<bool> id_seen(count + 1, false);
    std::vector<bool> order_seen(count + 1, false);
    manifest.pages.resize(count);

    for (const JsonValue& page : *pages) {
        if (page.get_object() == nullptr) return fail(ErrorCode::ManifestInvalidField, "every entry of pages must be an object");

        auto id = required_string(page, "id");
        if (!id) return unexpected(std::move(id).error());
        if (!is_valid_page_id(*id.value())) return fail(ErrorCode::InvalidIdentifier, "invalid page id \"" + *id.value() + "\"");

        const JsonValue* order_value = page.find("order");
        if (order_value == nullptr) return fail(ErrorCode::ManifestInvalidField, "page entry without \"order\"");
        const std::int64_t* order = order_value->get_integer();
        if (order == nullptr) return fail(ErrorCode::ManifestInvalidField, "field \"order\" must be an integer");

        const std::size_t number = std::stoul(id.value()->substr(1));
        if (number < 1 || number > count) return fail(ErrorCode::ManifestPagesInvalid, "page id \"" + *id.value() + "\" is outside p00001 to pNNNNN");
        if (id_seen[number]) return fail(ErrorCode::ManifestPagesInvalid, "duplicate page id \"" + *id.value() + "\"");
        id_seen[number] = true;

        if (*order < 1 || static_cast<std::uint64_t>(*order) > count) {
            return fail(ErrorCode::ManifestPagesInvalid, "page order " + std::to_string(*order) + " is outside 1 to the number of pages");
        }
        const auto position = static_cast<std::size_t>(*order);
        if (order_seen[position]) return fail(ErrorCode::ManifestPagesInvalid, "duplicate page order " + std::to_string(*order));
        order_seen[position] = true;

        manifest.pages[position - 1] = PageEntry{*id.value(), static_cast<std::uint32_t>(position)};
    }
    return manifest;
}

Result<void, FileError> check_page_directories(const ZipArchive& archive, const Manifest& manifest) {
    constexpr std::string_view kPrefix = "pages/";
    std::unordered_set<std::string> present;
    for (const ZipEntryInfo& entry : archive.entries()) {
        const std::string_view name = entry.name;
        if (name.size() > kPrefix.size() + 6 && name.substr(0, kPrefix.size()) == kPrefix &&
            name[kPrefix.size() + 6] == '/') {
            present.insert(std::string(name.substr(kPrefix.size(), 6)));
        }
    }

    for (const PageEntry& page : manifest.pages) {
        if (!present.contains(page.id)) {
            return unexpected(FileError{ErrorCode::PageDirectoryMissing, "page directory is missing", "pages/" + page.id + "/"});
        }
    }
    return {};
}

Result<Manifest, FileError> read_manifest(const ZipArchive& archive) {
    auto json = read_manifest_json(archive);
    if (!json) return unexpected(std::move(json).error());

    auto manifest = parse_manifest(json.value());
    if (!manifest) return unexpected(std::move(manifest).error());

    auto directories = check_page_directories(archive, manifest.value());
    if (!directories) return unexpected(std::move(directories).error());
    return std::move(manifest).value();
}

}  // namespace onf
