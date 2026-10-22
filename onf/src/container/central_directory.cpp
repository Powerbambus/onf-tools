#include "central_directory.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace onf {

namespace {

constexpr std::uint32_t kEocdSignature = 0x06054b50;
constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50;
constexpr std::uint32_t kZip64EocdSignature = 0x06064b50;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50;

constexpr std::size_t kEocdSize = 22;
constexpr std::size_t kZip64LocatorSize = 20;
constexpr std::size_t kZip64EocdSize = 56;
constexpr std::size_t kCentralHeaderSize = 46;
constexpr std::size_t kMaxCommentSize = 0xFFFF;

constexpr std::uint16_t kFlagEncrypted = 1u << 0;

Unexpected<FileError> fail(ErrorCode code, std::string message, std::string entry = {}) {
    return unexpected(FileError{code, std::move(message), std::move(entry)});
}

struct Directory {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t count = 0;
};

std::uint16_t u16(const std::vector<std::uint8_t>& buffer, std::size_t at) {
    return static_cast<std::uint16_t>(buffer[at] | (buffer[at + 1] << 8));
}

std::uint32_t u32(const std::vector<std::uint8_t>& buffer, std::size_t at) {
    return static_cast<std::uint32_t>(u16(buffer, at)) | (static_cast<std::uint32_t>(u16(buffer, at + 2)) << 16);
}

std::uint64_t u64(const std::vector<std::uint8_t>& buffer, std::size_t at) {
    return static_cast<std::uint64_t>(u32(buffer, at)) | (static_cast<std::uint64_t>(u32(buffer, at + 4)) << 32);
}

Result<std::vector<std::uint8_t>, FileError> read_exact(const ByteSource& source, std::uint64_t offset, std::size_t length) {
    std::vector<std::uint8_t> buffer(length);
    if (!source.read_at(offset, buffer)) return fail(ErrorCode::ZipOpenFailed, "the archive is truncated");
    return buffer;
}

Result<Directory, FileError> locate_directory(const ByteSource& source) {
    const std::uint64_t size = source.size();
    if (size < kEocdSize) return fail(ErrorCode::ZipOpenFailed, "the file is too small to be a ZIP archive");

    const std::size_t tail_length = static_cast<std::size_t>(std::min<std::uint64_t>(size, kEocdSize + kMaxCommentSize));
    const std::uint64_t tail_offset = size - tail_length;
    auto tail = read_exact(source, tail_offset, tail_length);
    if (!tail) return unexpected(std::move(tail).error());

    std::size_t position = tail_length - kEocdSize;
    bool found = false;
    while (true) {
        if (u32(tail.value(), position) == kEocdSignature &&
            u16(tail.value(), position + 20) == tail_length - position - kEocdSize) {
            found = true;
            break;
        }
        if (position == 0) break;
        --position;
    }
    if (!found) return fail(ErrorCode::ZipOpenFailed, "no end of central directory record found");

    const std::vector<std::uint8_t>& t = tail.value();
    const std::uint64_t eocd_offset = tail_offset + position;
    const std::uint16_t disk = u16(t, position + 4);
    const std::uint16_t directory_disk = u16(t, position + 6);
    const std::uint16_t count_on_disk = u16(t, position + 8);
    const std::uint16_t count_total = u16(t, position + 10);

    if (disk != 0 || directory_disk != 0 || count_on_disk != count_total) {
        return fail(ErrorCode::ZipMultiDisk, "multi-disk archives are not supported");
    }

    Directory directory{u32(t, position + 16), u32(t, position + 12), count_total};

    const bool needs_zip64 = count_total == 0xFFFF || directory.size == 0xFFFFFFFFu || directory.offset == 0xFFFFFFFFu;
    if (needs_zip64) {
        if (eocd_offset < kZip64LocatorSize) return fail(ErrorCode::ZipOpenFailed, "the ZIP64 locator is missing");
        auto locator = read_exact(source, eocd_offset - kZip64LocatorSize, kZip64LocatorSize);
        if (!locator) return unexpected(std::move(locator).error());
        const std::vector<std::uint8_t>& l = locator.value();
        if (u32(l, 0) != kZip64LocatorSignature) return fail(ErrorCode::ZipOpenFailed, "the ZIP64 locator is missing");
        if (u32(l, 4) != 0 || u32(l, 16) != 1) return fail(ErrorCode::ZipMultiDisk, "multi-disk archives are not supported");

        const std::uint64_t record_offset = u64(l, 8);
        if (record_offset > size || size - record_offset < kZip64EocdSize) {
            return fail(ErrorCode::ZipOpenFailed, "the ZIP64 end of central directory record is outside the file");
        }
        auto record = read_exact(source, record_offset, kZip64EocdSize);
        if (!record) return unexpected(std::move(record).error());
        const std::vector<std::uint8_t>& r = record.value();
        if (u32(r, 0) != kZip64EocdSignature) return fail(ErrorCode::ZipOpenFailed, "invalid ZIP64 end of central directory record");
        if (u32(r, 16) != 0 || u32(r, 20) != 0 || u64(r, 24) != u64(r, 32)) {
            return fail(ErrorCode::ZipMultiDisk, "multi-disk archives are not supported");
        }
        directory = Directory{u64(r, 48), u64(r, 40), u64(r, 32)};
    }

    if (directory.offset > size || directory.size > size - directory.offset) {
        return fail(ErrorCode::ZipOpenFailed, "the central directory is outside the file");
    }
    if (directory.count > directory.size / kCentralHeaderSize) {
        return fail(ErrorCode::ZipOpenFailed, "the entry count does not fit into the central directory");
    }
    return directory;
}

}  // namespace

Result<std::vector<CentralEntry>, FileError> scan_central_directory(const ByteSource& source) {
    auto located = locate_directory(source);
    if (!located) return unexpected(std::move(located).error());
    const Directory directory = located.value();

    std::vector<CentralEntry> entries;
    entries.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(directory.count, 1024)));
    std::unordered_set<std::string> names;
    std::uint64_t position = 0;

    for (std::uint64_t i = 0; i < directory.count; ++i) {
        if (directory.size - position < kCentralHeaderSize) return fail(ErrorCode::ZipOpenFailed, "the central directory is truncated");
        auto header = read_exact(source, directory.offset + position, kCentralHeaderSize);
        if (!header) return unexpected(std::move(header).error());
        const std::vector<std::uint8_t>& h = header.value();

        if (u32(h, 0) != kCentralHeaderSignature) return fail(ErrorCode::ZipOpenFailed, "invalid central directory header");
        const std::uint16_t flags = u16(h, 8);
        const std::uint16_t name_length = u16(h, 28);
        const std::uint64_t record_length = kCentralHeaderSize + std::uint64_t{name_length} + u16(h, 30) + u16(h, 32);
        if (directory.size - position < record_length) return fail(ErrorCode::ZipOpenFailed, "the central directory is truncated");

        auto name_bytes = read_exact(source, directory.offset + position + kCentralHeaderSize, name_length);
        if (!name_bytes) return unexpected(std::move(name_bytes).error());
        std::string name(name_bytes.value().begin(), name_bytes.value().end());

        if (u16(h, 34) != 0) return fail(ErrorCode::ZipMultiDisk, "multi-disk archives are not supported", name);
        if ((flags & kFlagEncrypted) != 0) return fail(ErrorCode::ZipEncryptedEntry, "encrypted entries are not supported", name);
        if (!names.insert(name).second) return fail(ErrorCode::ZipDuplicateEntry, "duplicate entry name", name);

        entries.push_back(CentralEntry{std::move(name), flags});
        position += record_length;
    }
    return entries;
}

}  // namespace onf
