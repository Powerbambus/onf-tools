#pragma once

#include <zlib.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "support/strokes_builder.h"

namespace onf::test {

inline Bytes deflate_raw(const Bytes& data, int level = 9) {
    z_stream stream{};
    deflateInit2(&stream, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    Bytes out(deflateBound(&stream, static_cast<uLong>(data.size())) + 16);
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return out;
}

inline std::uint32_t crc32_of(const Bytes& data) {
    return static_cast<std::uint32_t>(crc32(0, data.data(), static_cast<uInt>(data.size())));
}

struct ZipEntrySpec {
    std::string name;
    Bytes data;
    std::uint16_t method = 0;  // 0 stored, 8 deflate, anything else stores the data unchanged
    int deflate_level = 9;
    std::uint16_t flags = 0;
    std::uint32_t external_attributes = 0;
    std::uint8_t host = 3;  // 3 = Unix
    std::optional<std::uint64_t> declared_size;
    std::optional<std::uint64_t> declared_compressed_size;
    std::optional<std::uint32_t> crc;
    std::optional<std::string> local_name;
    std::uint16_t disk_start = 0;
    std::size_t extra_payload_bytes = 0;  // appended to the stored payload, for example an encryption header
    bool zip64_extra = false;
};

struct ZipSpec {
    std::vector<ZipEntrySpec> entries;
    std::uint16_t eocd_disk = 0;
    std::uint16_t eocd_directory_disk = 0;
    std::optional<std::uint16_t> entries_on_disk;
    bool zip64_eocd = false;
    std::string comment;
};

inline Bytes build_zip(const ZipSpec& spec) {
    Bytes out;
    Bytes directory;

    for (const ZipEntrySpec& e : spec.entries) {
        Bytes payload = e.method == 8 ? deflate_raw(e.data, e.deflate_level) : e.data;
        payload.insert(payload.end(), e.extra_payload_bytes, 0);

        const std::uint64_t size = e.declared_size.value_or(e.data.size());
        const std::uint64_t compressed = e.declared_compressed_size.value_or(payload.size());
        const std::uint32_t crc = e.crc.value_or(crc32_of(e.data));
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        const std::string local_name = e.local_name.value_or(e.name);

        Bytes local_extra;
        Bytes central_extra;
        std::uint32_t size32 = static_cast<std::uint32_t>(size);
        std::uint32_t compressed32 = static_cast<std::uint32_t>(compressed);
        std::uint32_t offset32 = offset;
        if (e.zip64_extra) {
            size32 = compressed32 = offset32 = 0xFFFFFFFFu;
            append_u16(local_extra, 1);
            append_u16(local_extra, 16);
            append_u64(local_extra, size);
            append_u64(local_extra, compressed);
            append_u16(central_extra, 1);
            append_u16(central_extra, 24);
            append_u64(central_extra, size);
            append_u64(central_extra, compressed);
            append_u64(central_extra, offset);
        }

        append_u32(out, 0x04034b50);
        append_u16(out, 20);
        append_u16(out, e.flags);
        append_u16(out, e.method);
        append_u16(out, 0);
        append_u16(out, 0x21);
        append_u32(out, crc);
        append_u32(out, compressed32);
        append_u32(out, size32);
        append_u16(out, static_cast<std::uint16_t>(local_name.size()));
        append_u16(out, static_cast<std::uint16_t>(local_extra.size()));
        out.insert(out.end(), local_name.begin(), local_name.end());
        append(out, local_extra);
        append(out, payload);

        append_u32(directory, 0x02014b50);
        append_u16(directory, static_cast<std::uint16_t>((e.host << 8) | 20));
        append_u16(directory, 20);
        append_u16(directory, e.flags);
        append_u16(directory, e.method);
        append_u16(directory, 0);
        append_u16(directory, 0x21);
        append_u32(directory, crc);
        append_u32(directory, compressed32);
        append_u32(directory, size32);
        append_u16(directory, static_cast<std::uint16_t>(e.name.size()));
        append_u16(directory, static_cast<std::uint16_t>(central_extra.size()));
        append_u16(directory, 0);
        append_u16(directory, e.disk_start);
        append_u16(directory, 0);
        append_u32(directory, e.external_attributes);
        append_u32(directory, offset32);
        directory.insert(directory.end(), e.name.begin(), e.name.end());
        append(directory, central_extra);
    }

    const std::uint64_t directory_offset = out.size();
    const std::uint64_t count = spec.entries.size();
    append(out, directory);

    const bool zip64 = spec.zip64_eocd || count >= 0xFFFF;
    if (zip64) {
        const std::uint64_t record_offset = out.size();
        append_u32(out, 0x06064b50);
        append_u64(out, 44);
        append_u16(out, 45);
        append_u16(out, 45);
        append_u32(out, 0);
        append_u32(out, 0);
        append_u64(out, count);
        append_u64(out, count);
        append_u64(out, directory.size());
        append_u64(out, directory_offset);

        append_u32(out, 0x07064b50);
        append_u32(out, 0);
        append_u64(out, record_offset);
        append_u32(out, 1);
    }

    append_u32(out, 0x06054b50);
    append_u16(out, spec.eocd_disk);
    append_u16(out, spec.eocd_directory_disk);
    append_u16(out, zip64 ? 0xFFFF : spec.entries_on_disk.value_or(static_cast<std::uint16_t>(count)));
    append_u16(out, zip64 ? 0xFFFF : static_cast<std::uint16_t>(count));
    append_u32(out, zip64 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(directory.size()));
    append_u32(out, zip64 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(directory_offset));
    append_u16(out, static_cast<std::uint16_t>(spec.comment.size()));
    out.insert(out.end(), spec.comment.begin(), spec.comment.end());
    return out;
}

}  // namespace onf::test
