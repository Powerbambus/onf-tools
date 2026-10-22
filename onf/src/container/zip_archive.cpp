#include "onf/container/zip_archive.h"

#include <zip.h>

#include <algorithm>
#include <unordered_map>

#include "central_directory.h"
#include "onf/container/paths.h"
#include "onf/limits.h"

namespace onf {

struct ZipArchive::Impl {
    explicit Impl(ByteSource s) : source(std::move(s)) {}
    ~Impl() {
        if (archive != nullptr) zip_discard(archive);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ByteSource source;  // must outlive the libzip archive, which reads from its memory or file
    zip_t* archive = nullptr;
    std::vector<ZipEntryInfo> entries;
    std::unordered_map<std::string, std::size_t> index_by_name;
};

namespace {

constexpr std::uint32_t kFileTypeMask = 0170000;
constexpr std::uint32_t kFileTypeSymlink = 0120000;
constexpr std::size_t kReadBlock = 64 * 1024;

Unexpected<FileError> fail(ErrorCode code, std::string message, std::string entry = {}) {
    return unexpected(FileError{code, std::move(message), std::move(entry)});
}

bool exceeds_ratio(std::uint64_t size, std::uint64_t compressed_size) {
    if (size < limits::kRatioCheckMinSize) return false;
    if (compressed_size == 0) return true;
    if (compressed_size > UINT64_MAX / limits::kMaxCompressionRatio) return false;
    return size > limits::kMaxCompressionRatio * compressed_size;
}

Result<zip_t*, FileError> open_libzip(const ByteSource& source) {
    zip_error_t error;
    zip_error_init(&error);

    zip_source_t* zip_source = nullptr;
    if (const auto* path = source.file_path()) {
        zip_source = zip_source_file_create(path->c_str(), 0, -1, &error);
    } else {
        const auto bytes = source.memory();
        zip_source = zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error);
    }
    if (zip_source == nullptr) {
        FileError result{ErrorCode::ZipOpenFailed, zip_error_strerror(&error), {}};
        zip_error_fini(&error);
        return unexpected(std::move(result));
    }

    zip_t* archive = zip_open_from_source(zip_source, ZIP_RDONLY | ZIP_CHECKCONS, &error);
    if (archive == nullptr) {
        const int code = zip_error_code_zip(&error);
        FileError result{code == ZIP_ER_EXISTS ? ErrorCode::ZipDuplicateEntry
                         : code == ZIP_ER_MULTIDISK ? ErrorCode::ZipMultiDisk
                                                    : ErrorCode::ZipOpenFailed,
                         zip_error_strerror(&error), {}};
        zip_source_free(zip_source);
        zip_error_fini(&error);
        return unexpected(std::move(result));
    }
    zip_error_fini(&error);
    return archive;
}

}  // namespace

ZipArchive::ZipArchive(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ZipArchive::ZipArchive(ZipArchive&&) noexcept = default;
ZipArchive& ZipArchive::operator=(ZipArchive&&) noexcept = default;
ZipArchive::~ZipArchive() = default;

Result<ZipArchive, FileError> ZipArchive::open(ByteSource source) {
    auto central = scan_central_directory(source);
    if (!central) return unexpected(std::move(central).error());

    for (const CentralEntry& entry : central.value()) {
        const auto valid = validate_entry_name(entry.name);
        if (!valid) return fail(valid.error(), "invalid entry name", entry.name);
    }

    auto impl = std::make_unique<Impl>(std::move(source));
    auto opened = open_libzip(impl->source);
    if (!opened) return unexpected(std::move(opened).error());
    impl->archive = opened.value();

    const zip_int64_t count = zip_get_num_entries(impl->archive, 0);
    if (count < 0 || static_cast<std::uint64_t>(count) != central.value().size()) {
        return fail(ErrorCode::ZipOpenFailed, "the central directory is read differently by the two readers");
    }

    std::uint64_t total_size = 0;
    for (zip_int64_t i = 0; i < count; ++i) {
        const std::string& name = central.value()[static_cast<std::size_t>(i)].name;

        zip_stat_t stat;
        zip_stat_init(&stat);
        constexpr zip_uint64_t kNeeded =
            ZIP_STAT_NAME | ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE | ZIP_STAT_COMP_METHOD | ZIP_STAT_ENCRYPTION_METHOD;
        if (zip_stat_index(impl->archive, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_RAW, &stat) != 0 ||
            (stat.valid & kNeeded) != kNeeded) {
            return fail(ErrorCode::ZipOpenFailed, "incomplete entry information", name);
        }
        if (name != stat.name) {
            return fail(ErrorCode::ZipOpenFailed, "the central directory is read differently by the two readers", name);
        }

        if (stat.encryption_method != ZIP_EM_NONE) return fail(ErrorCode::ZipEncryptedEntry, "encrypted entries are not supported", name);
        if (stat.comp_method != ZIP_CM_STORE && stat.comp_method != ZIP_CM_DEFLATE) {
            return fail(ErrorCode::ZipUnsupportedMethod, "only stored and deflated entries are supported", name);
        }

        zip_uint8_t host = 0;
        zip_uint32_t attributes = 0;
        if (zip_file_get_external_attributes(impl->archive, static_cast<zip_uint64_t>(i), 0, &host, &attributes) == 0 &&
            host == ZIP_OPSYS_UNIX && ((attributes >> 16) & kFileTypeMask) == kFileTypeSymlink) {
            return fail(ErrorCode::ZipSymlinkEntry, "symbolic links are not supported", name);
        }

        if (stat.size > limits::kMaxEntryUncompressedSize) {
            return fail(ErrorCode::EntrySizeLimitExceeded, "entry exceeds the maximum uncompressed size", name);
        }
        if (exceeds_ratio(stat.size, stat.comp_size)) {
            return fail(ErrorCode::CompressionRatioExceeded, "entry exceeds the maximum compression ratio", name);
        }
        total_size += stat.size;
        if (total_size > limits::kMaxTotalUncompressedSize) {
            return fail(ErrorCode::TotalSizeLimitExceeded, "archive exceeds the maximum total uncompressed size", name);
        }

        impl->index_by_name.emplace(name, impl->entries.size());
        impl->entries.push_back(ZipEntryInfo{name, stat.size, stat.comp_size, !name.empty() && name.back() == '/'});
    }
    return ZipArchive(std::move(impl));
}

const std::vector<ZipEntryInfo>& ZipArchive::entries() const noexcept { return impl_->entries; }

std::optional<std::size_t> ZipArchive::find(std::string_view name) const {
    const auto it = impl_->index_by_name.find(std::string(name));
    if (it == impl_->index_by_name.end()) return std::nullopt;
    return it->second;
}

Result<std::vector<std::uint8_t>, FileError> ZipArchive::read(std::size_t index) const {
    if (index >= impl_->entries.size()) return fail(ErrorCode::ZipReadFailed, "no such entry");
    const ZipEntryInfo& info = impl_->entries[index];

    zip_file_t* file = zip_fopen_index(impl_->archive, index, 0);
    if (file == nullptr) return fail(ErrorCode::ZipReadFailed, zip_strerror(impl_->archive), info.name);

    std::vector<std::uint8_t> data;
    data.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(info.size, limits::kRatioCheckMinSize)));
    std::vector<std::uint8_t> block(kReadBlock);

    Unexpected<FileError> failure = fail(ErrorCode::ZipReadFailed, "unknown error", info.name);
    bool failed = false;
    while (true) {
        const zip_int64_t read = zip_fread(file, block.data(), block.size());
        if (read < 0) {
            failure = fail(ErrorCode::ZipReadFailed, zip_file_strerror(file), info.name);
            failed = true;
            break;
        }
        if (read == 0) break;
        if (static_cast<std::uint64_t>(read) > info.size - data.size()) {
            failure = fail(ErrorCode::EntrySizeLimitExceeded, "entry is larger than its declared size", info.name);
            failed = true;
            break;
        }
        data.insert(data.end(), block.begin(), block.begin() + read);
    }
    zip_fclose(file);

    if (failed) return failure;
    if (data.size() != info.size) return fail(ErrorCode::ZipReadFailed, "entry is smaller than its declared size", info.name);
    return data;
}

}  // namespace onf
