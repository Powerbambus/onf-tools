#include "onf/write/writer.h"

#include <zip.h>

#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "json_text.h"
#include "meta_encoder.h"
#include "onf/primitives/sha256.h"
#include "onf/write/strokes.h"

namespace onf {

namespace {

// One file of the archive. libzip keeps pointers into `data` until zip_close, so the entries are built completely
// before the first one is handed over and are not touched afterwards.
struct Entry {
    std::string name;
    std::vector<std::uint8_t> data;
    bool deflate = false;
};

std::vector<std::uint8_t> text_bytes(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }

// Compression as required by spec 4.5: manifest.json and meta.json are deflated, strokes.bin is stored. Assets and
// the thumbnail are already compressed and are stored.
std::vector<Entry> build_entries(const WriteDocument& document) {
    std::vector<Entry> entries;
    entries.push_back({"manifest.json", text_bytes(serialize_json(manifest_to_json(document))), true});

    for (std::size_t i = 0; i < document.pages.size(); ++i) {
        const std::string directory = "pages/" + page_directory_name(i + 1) + "/";
        entries.push_back({directory + "meta.json", text_bytes(serialize_json(page_meta_to_json(document.pages[i].meta))), true});
        entries.push_back({directory + "strokes.bin", encode_strokes_file(document.pages[i].ink), false});
    }

    // Identical files are stored once (spec 9.2.1).
    std::vector<std::string> written;
    for (const WriteAsset& asset : document.assets) {
        const std::string name = "assets/" + sha256_hex(asset.bytes) + "." + asset.extension;
        bool duplicate = false;
        for (const std::string& other : written) duplicate = duplicate || other == name;
        if (duplicate) continue;
        written.push_back(name);
        entries.push_back({name, asset.bytes, false});
    }
    if (document.thumbnail) entries.push_back({"thumbnail.png", *document.thumbnail, false});
    return entries;
}

std::time_t unix_time(const UtcDateTime& t) {
    using namespace std::chrono;
    const sys_days day = year_month_day{year{t.year}, month{t.month}, std::chrono::day{t.day}};
    return static_cast<std::time_t>(duration_cast<seconds>(day.time_since_epoch()).count()) + t.hour * 3600 + t.minute * 60 + t.second;
}

FileError write_failure(const std::string& message, const std::filesystem::path& path) {
    return FileError{ErrorCode::WriteFailed, message, path.string()};
}

}  // namespace

Result<void, FileError> write_onf(const WriteDocument& document, const std::filesystem::path& path) {
    if (auto message = check_writable(document)) return unexpected(FileError{ErrorCode::WriteInvalidDocument, *message, {}});

    const std::vector<Entry> entries = build_entries(document);
    const std::time_t modified = unix_time(document.modified_at);

    // The archive is built beside the target and moved over it, so a failure leaves no half-written file.
    std::filesystem::path temporary = path;
    temporary += ".part";

    int error_code = 0;
    zip_t* archive = zip_open(temporary.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error_code);
    if (archive == nullptr) {
        zip_error_t error;
        zip_error_init_with_code(&error, error_code);
        FileError failure = write_failure(std::string("cannot create the archive: ") + zip_error_strerror(&error), path);
        zip_error_fini(&error);
        return unexpected(std::move(failure));
    }

    const auto fail = [&](const std::string& what) {
        FileError failure = write_failure(what + ": " + zip_strerror(archive), path);
        zip_discard(archive);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return unexpected(std::move(failure));
    };

    // The assets/ directory has to be an explicit entry (spec 4.2).
    // Its time is set too: libzip would use the current time, and the same document must give the same bytes.
    const zip_int64_t directory = zip_dir_add(archive, "assets", ZIP_FL_ENC_UTF_8);
    if (directory < 0) return fail("cannot add assets/");
    if (zip_file_set_mtime(archive, static_cast<zip_uint64_t>(directory), modified, 0) < 0) return fail("cannot set the time of assets/");

    for (const Entry& entry : entries) {
        zip_source_t* source = zip_source_buffer(archive, entry.data.data(), entry.data.size(), 0);
        if (source == nullptr) return fail("cannot add " + entry.name);

        const zip_int64_t index = zip_file_add(archive, entry.name.c_str(), source, ZIP_FL_ENC_UTF_8);
        if (index < 0) {
            zip_source_free(source);
            return fail("cannot add " + entry.name);
        }
        const auto uindex = static_cast<zip_uint64_t>(index);
        if (zip_set_file_compression(archive, uindex, entry.deflate ? ZIP_CM_DEFLATE : ZIP_CM_STORE, 0) < 0) return fail("cannot set the compression of " + entry.name);
        // A regular file with mode 0644 from a Unix host, and the time of the last change of the document.
        if (zip_file_set_external_attributes(archive, uindex, 0, ZIP_OPSYS_UNIX, (0100644u << 16)) < 0) return fail("cannot set the attributes of " + entry.name);
        if (zip_file_set_mtime(archive, uindex, modified, 0) < 0) return fail("cannot set the time of " + entry.name);
    }

    if (zip_close(archive) < 0) return fail("cannot finish the archive");

    std::error_code rename_error;
    std::filesystem::rename(temporary, path, rename_error);
    if (rename_error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return unexpected(write_failure("cannot move the archive into place: " + rename_error.message(), path));
    }
    return {};
}

}  // namespace onf
