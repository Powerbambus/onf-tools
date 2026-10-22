#include "doctest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/container/zip_archive.h"
#include "onf/errors.h"
#include "onf/limits.h"
#include "support/zip_builder.h"

using namespace onf;
using namespace onf::test;

namespace {

constexpr std::uint64_t kGiB = 1ull << 30;

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

Result<ZipArchive, FileError> open_bytes(Bytes bytes) { return ZipArchive::open(ByteSource::from_memory(std::move(bytes))); }

Result<ZipArchive, FileError> open_spec(const ZipSpec& spec) { return open_bytes(build_zip(spec)); }

ZipArchive open_ok(const ZipSpec& spec) {
    auto result = open_spec(spec);
    REQUIRE_MESSAGE(result.has_value(), "unexpected error: " << error_name(result.error().code) << ": " << result.error().message);
    return std::move(result).value();
}

ErrorCode error_of(const ZipSpec& spec) {
    const auto result = open_spec(spec);
    REQUIRE_MESSAGE(!result.has_value(), "unexpectedly accepted");
    return result.error().code;
}

ZipSpec single(ZipEntrySpec entry) {
    ZipSpec spec;
    spec.entries.push_back(std::move(entry));
    return spec;
}

ZipEntrySpec file(std::string name, std::string content = "hello", std::uint16_t method = 0) {
    ZipEntrySpec e;
    e.name = std::move(name);
    e.data = text(content);
    e.method = method;
    return e;
}

// A deflate entry that declares `size` bytes. The compressed data is padded so that `compressed` bytes really lie
// in the archive, which libzip checks when it opens it.
ZipEntrySpec padded_deflate(std::uint64_t size, std::uint64_t compressed) {
    ZipEntrySpec e = file("x.bin", "tiny", 8);
    const std::size_t real = deflate_raw(e.data).size();
    REQUIRE(compressed >= real);
    e.declared_size = size;
    e.extra_payload_bytes = static_cast<std::size_t>(compressed - real);
    e.zip64_extra = size >= 0xFFFFFFFFull;
    return e;
}

std::uint64_t real_tiny_size() { return deflate_raw(text("tiny")).size(); }

}  // namespace

TEST_CASE("zip: a plain archive") {
    ZipSpec spec;
    spec.entries.push_back(file("manifest.json", "{}"));
    spec.entries.push_back(file("pages/"));
    spec.entries.back().data.clear();
    spec.entries.push_back(file("pages/p00001/meta.json", std::string(1000, 'x'), 8));

    const ZipArchive archive = open_ok(spec);
    REQUIRE(archive.entries().size() == 3);
    CHECK(archive.entries()[0].name == "manifest.json");
    CHECK_FALSE(archive.entries()[0].is_directory);
    CHECK(archive.entries()[0].size == 2);
    CHECK(archive.entries()[1].is_directory);
    CHECK(archive.entries()[2].size == 1000);
    CHECK(archive.entries()[2].compressed_size < 1000);

    CHECK(archive.find("manifest.json") == 0);
    CHECK(archive.find("pages/p00001/meta.json") == 2);
    CHECK_FALSE(archive.find("missing").has_value());
    CHECK_FALSE(archive.find("Manifest.json").has_value());

    CHECK(archive.read(0).value() == text("{}"));
    CHECK(archive.read(2).value() == text(std::string(1000, 'x')));
    CHECK(archive.read(1).value().empty());
}

TEST_CASE("zip: an archive without entries is valid at container level") {
    const ZipArchive archive = open_ok(ZipSpec{});
    CHECK(archive.entries().empty());
}

TEST_CASE("zip: the archive can be moved and still reads") {
    ZipArchive first = open_ok(single(file("a.txt", "payload")));
    ZipArchive second = std::move(first);
    CHECK(second.read(0).value() == text("payload"));
    ZipArchive third = open_ok(single(file("b.txt", "other")));
    third = std::move(second);
    CHECK(third.read(0).value() == text("payload"));
}

TEST_CASE("zip: input that is not an archive") {
    CHECK_FALSE(open_bytes({}).has_value());
    CHECK_FALSE(open_bytes(text("not a zip file at all, just text")).has_value());
    CHECK_FALSE(open_bytes(Bytes(21, 0)).has_value());
    CHECK_FALSE(open_bytes(Bytes(100000, 0x50)).has_value());
    CHECK(open_bytes(text("short")).error().code == ErrorCode::ZipOpenFailed);
}

TEST_CASE("zip: an archive cut off at any length is rejected") {
    ZipSpec spec;
    spec.entries.push_back(file("manifest.json", "{}"));
    spec.entries.push_back(file("pages/p00001/meta.json", std::string(300, 'y'), 8));
    const Bytes full = build_zip(spec);
    REQUIRE(open_bytes(full).has_value());

    for (std::size_t length = 0; length < full.size(); ++length) {
        CAPTURE(length);
        CHECK_FALSE(open_bytes(Bytes(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length))).has_value());
    }
}

TEST_CASE("zip: encrypted entries") {
    ZipEntrySpec traditional = file("secret.bin");
    traditional.flags = 1;
    traditional.extra_payload_bytes = 12;
    CHECK(error_of(single(traditional)) == ErrorCode::ZipEncryptedEntry);

    ZipEntrySpec strong = file("secret.bin");
    strong.flags = 1 | 0x40;
    strong.extra_payload_bytes = 12;
    CHECK(error_of(single(strong)) == ErrorCode::ZipEncryptedEntry);

    ZipEntrySpec aes = file("secret.bin");
    aes.flags = 1;
    aes.method = 99;
    CHECK(error_of(single(aes)) == ErrorCode::ZipEncryptedEntry);

    ZipSpec second;
    second.entries.push_back(file("plain.txt"));
    second.entries.push_back(traditional);
    CHECK(error_of(second) == ErrorCode::ZipEncryptedEntry);
}

TEST_CASE("zip: multi-disk archives") {
    ZipSpec on_disk = single(file("a.txt"));
    on_disk.eocd_disk = 1;
    CHECK(error_of(on_disk) == ErrorCode::ZipMultiDisk);

    ZipSpec directory_disk = single(file("a.txt"));
    directory_disk.eocd_directory_disk = 1;
    CHECK(error_of(directory_disk) == ErrorCode::ZipMultiDisk);

    ZipSpec fewer_here = single(file("a.txt"));
    fewer_here.entries_on_disk = 0;
    CHECK(error_of(fewer_here) == ErrorCode::ZipMultiDisk);

    ZipEntrySpec elsewhere = file("a.txt");
    elsewhere.disk_start = 1;
    CHECK(error_of(single(elsewhere)) == ErrorCode::ZipMultiDisk);
}

TEST_CASE("zip: duplicate names are compared byte for byte") {
    ZipSpec duplicate;
    duplicate.entries.push_back(file("a.txt", "one"));
    duplicate.entries.push_back(file("b.txt"));
    duplicate.entries.push_back(file("a.txt", "two"));
    CHECK(error_of(duplicate) == ErrorCode::ZipDuplicateEntry);

    ZipSpec directories;
    directories.entries.push_back(file("assets/"));
    directories.entries.push_back(file("assets/"));
    CHECK(error_of(directories) == ErrorCode::ZipDuplicateEntry);

    ZipSpec different;
    different.entries.push_back(file("a.txt"));
    different.entries.push_back(file("A.txt"));
    different.entries.push_back(file("a"));
    different.entries.push_back(file("a/"));
    CHECK(open_ok(different).entries().size() == 4);
}

TEST_CASE("zip: unsafe and invalid entry names reject the archive") {
    for (const std::string& name :
         {std::string("../x"), std::string("a/../b"), std::string("/abs"), std::string("a\\b"), std::string("C:\\x"),
          std::string("a//b"), std::string("./a"), std::string("a/./b"), std::string("a\x01" "b"), std::string("a\x7f" "b"),
          std::string("pages/p00001/meta.json\0evil", 26), std::string("a\0b", 3)}) {
        CAPTURE(name);
        CHECK(error_of(single(file(name))) == ErrorCode::UnsafePath);
    }
    CHECK(error_of(single(file("caf\x82"))) == ErrorCode::EntryNameInvalidUtf8);
    CHECK(error_of(single(file("a\xFF" "b"))) == ErrorCode::EntryNameInvalidUtf8);

    ZipSpec unknown_entry;
    unknown_entry.entries.push_back(file("manifest.json", "{}"));
    unknown_entry.entries.push_back(file("extra/../../evil"));
    CHECK(error_of(unknown_entry) == ErrorCode::UnsafePath);

    CHECK(open_ok(single(file("unknown-file-from-the-future.dat"))).entries().size() == 1);
}

TEST_CASE("zip: symbolic links are rejected, other file types are not") {
    ZipEntrySpec link = file("link");
    link.external_attributes = 0120777u << 16;
    CHECK(error_of(single(link)) == ErrorCode::ZipSymlinkEntry);

    ZipEntrySpec regular = file("f");
    regular.external_attributes = 0100644u << 16;
    CHECK(open_ok(single(regular)).entries().size() == 1);

    ZipEntrySpec directory = file("d/");
    directory.external_attributes = 040755u << 16;
    CHECK(open_ok(single(directory)).entries().size() == 1);

    ZipEntrySpec from_dos = file("link");
    from_dos.external_attributes = 0120777u << 16;
    from_dos.host = 0;
    CHECK(open_ok(single(from_dos)).entries().size() == 1);
}

TEST_CASE("zip: only stored and deflated entries are supported") {
    for (std::uint16_t method : std::vector<std::uint16_t>{1, 6, 9, 12, 14, 93, 95}) {
        CAPTURE(method);
        ZipEntrySpec e = file("a.bin");
        e.method = method;
        CHECK(error_of(single(e)) == ErrorCode::ZipUnsupportedMethod);
    }
    CHECK(open_ok(single(file("a", "x", 0))).entries().size() == 1);
    CHECK(open_ok(single(file("a", "x", 8))).entries().size() == 1);
}

TEST_CASE("zip: size limit of a single entry") {
    const std::uint64_t limit = limits::kMaxEntryUncompressedSize;
    const std::uint64_t smallest_allowed_compressed = (limit + 99) / 100;

    CHECK(open_ok(single(padded_deflate(limit, smallest_allowed_compressed))).entries().size() == 1);
    CHECK(error_of(single(padded_deflate(limit + 1, real_tiny_size()))) == ErrorCode::EntrySizeLimitExceeded);
    CHECK(error_of(single(padded_deflate(4 * kGiB, real_tiny_size()))) == ErrorCode::EntrySizeLimitExceeded);
    CHECK(error_of(single(padded_deflate(64 * kGiB, real_tiny_size()))) == ErrorCode::EntrySizeLimitExceeded);
}

TEST_CASE("zip: size limit of the whole archive") {
    const std::uint64_t limit = limits::kMaxEntryUncompressedSize;
    const std::uint64_t compressed = (limit + 99) / 100;

    ZipSpec five;
    for (int i = 0; i < 5; ++i) {
        five.entries.push_back(padded_deflate(limit, compressed));
        five.entries.back().name = "e" + std::to_string(i);
    }
    CHECK(open_ok(five).entries().size() == 5);

    ZipEntrySpec one_more = padded_deflate(1, real_tiny_size());
    one_more.name = "last";
    five.entries.push_back(one_more);
    CHECK(error_of(five) == ErrorCode::TotalSizeLimitExceeded);
}

TEST_CASE("zip: compression ratio of 100:1 from 1 MiB on") {
    const std::uint64_t mib = limits::kRatioCheckMinSize;
    const std::uint64_t tiny = real_tiny_size();

    CHECK(open_ok(single(padded_deflate(mib - 1, tiny))).entries().size() == 1);
    CHECK(open_ok(single(padded_deflate(mib, 10486))).entries().size() == 1);
    CHECK(open_ok(single(padded_deflate(1048600, 10486))).entries().size() == 1);
    CHECK(error_of(single(padded_deflate(1048601, 10486))) == ErrorCode::CompressionRatioExceeded);
    CHECK(error_of(single(padded_deflate(mib, 10485))) == ErrorCode::CompressionRatioExceeded);
    CHECK(error_of(single(padded_deflate(mib, tiny))) == ErrorCode::CompressionRatioExceeded);
    CHECK(error_of(single(padded_deflate(limits::kMaxEntryUncompressedSize, tiny))) == ErrorCode::CompressionRatioExceeded);
}

TEST_CASE("zip: a declared compressed size of zero never passes") {
    ZipEntrySpec zero = file("x.bin", "tiny", 8);
    zero.declared_size = limits::kRatioCheckMinSize;
    zero.declared_compressed_size = 0;
    const ErrorCode code = error_of(single(zero));
    CHECK((code == ErrorCode::CompressionRatioExceeded || code == ErrorCode::ZipOpenFailed));
}

TEST_CASE("zip: an entry never produces more bytes than it declares") {
    ZipEntrySpec bomb = file("bomb.bin", "", 8);
    bomb.data = Bytes(3'000'000, 0);

    for (std::uint64_t declared : {std::uint64_t{10}, std::uint64_t{1}, limits::kRatioCheckMinSize - 1}) {
        CAPTURE(declared);
        bomb.declared_size = declared;
        ZipArchive archive = open_ok(single(bomb));
        const auto result = archive.read(0);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::EntrySizeLimitExceeded);
    }

    bomb.declared_size = 3'000'000;
    CHECK(error_of(single(bomb)) == ErrorCode::CompressionRatioExceeded);
}

TEST_CASE("zip: an entry that produces exactly its declared size is read completely") {
    std::mt19937_64 rng(99);
    ZipEntrySpec e = file("noise.bin", "", 8);
    for (int i = 0; i < 5000; ++i) e.data.push_back(static_cast<std::uint8_t>(rng()));
    const ZipArchive archive = open_ok(single(e));
    CHECK(archive.read(0).value() == e.data);
}

TEST_CASE("zip: an entry smaller than declared and a wrong CRC fail while reading") {
    ZipEntrySpec small = file("a.bin", "0123456789", 8);
    small.declared_size = 1000;
    const auto short_result = open_ok(single(small)).read(0);
    REQUIRE_FALSE(short_result.has_value());
    CHECK(short_result.error().code == ErrorCode::ZipReadFailed);

    ZipEntrySpec bad_crc = file("a.bin", "0123456789");
    bad_crc.crc = 12345;
    const auto crc_result = open_spec(single(bad_crc));
    if (crc_result.has_value()) {
        CHECK_FALSE(crc_result.value().read(0).has_value());
    }
    ZipEntrySpec bad_crc_deflate = file("a.bin", "0123456789", 8);
    bad_crc_deflate.crc = 12345;
    const auto deflate_result = open_spec(single(bad_crc_deflate));
    if (deflate_result.has_value()) {
        CHECK_FALSE(deflate_result.value().read(0).has_value());
    }
}

TEST_CASE("zip: local and central header must agree") {
    ZipEntrySpec e = file("a.txt");
    e.local_name = "b.txt";
    CHECK(error_of(single(e)) == ErrorCode::ZipOpenFailed);
}

TEST_CASE("zip: reading a missing entry") {
    const ZipArchive archive = open_ok(single(file("a.txt")));
    CHECK(archive.read(1).error().code == ErrorCode::ZipReadFailed);
    CHECK(archive.read(static_cast<std::size_t>(-1)).error().code == ErrorCode::ZipReadFailed);
}

TEST_CASE("zip: ZIP64") {
    ZipSpec spec;
    spec.entries.push_back(file("manifest.json", "{}"));
    spec.entries.push_back(file("pages/p00001/meta.json", "meta", 8));
    spec.zip64_eocd = true;
    const ZipArchive archive = open_ok(spec);
    CHECK(archive.entries().size() == 2);
    CHECK(archive.read(1).value() == text("meta"));

    ZipSpec with_extra = single(file("a.txt", "zip64 entry"));
    with_extra.entries[0].zip64_extra = true;
    const ZipArchive extra_archive = open_ok(with_extra);
    CHECK(extra_archive.read(0).value() == text("zip64 entry"));
}

TEST_CASE("zip: more than 65535 entries need ZIP64") {
    ZipSpec spec;
    for (int i = 0; i < 65536; ++i) spec.entries.push_back(file("e" + std::to_string(i), ""));
    const ZipArchive archive = open_ok(spec);
    CHECK(archive.entries().size() == 65536);
    CHECK(archive.find("e65535") == 65535);
}

TEST_CASE("zip: a broken ZIP64 record") {
    ZipSpec spec = single(file("a.txt"));
    spec.zip64_eocd = true;
    Bytes bytes = build_zip(spec);
    const std::size_t eocd = bytes.size() - 22;
    const std::size_t locator = eocd - 20;
    REQUIRE(open_bytes(bytes).has_value());

    Bytes no_locator = bytes;
    no_locator[locator] ^= 0xFF;
    CHECK_FALSE(open_bytes(no_locator).has_value());

    Bytes two_disks = bytes;
    two_disks[locator + 16] = 2;
    CHECK(open_bytes(two_disks).error().code == ErrorCode::ZipMultiDisk);

    Bytes far_away = bytes;
    for (std::size_t i = 8; i < 16; ++i) far_away[locator + i] = 0xFF;
    CHECK_FALSE(open_bytes(far_away).has_value());
}

TEST_CASE("zip: an absurd entry count fails without allocating") {
    ZipSpec spec = single(file("a.txt"));
    spec.zip64_eocd = true;
    const Bytes bytes = build_zip(spec);
    const std::size_t locator = bytes.size() - 22 - 20;
    std::uint64_t record_offset = 0;
    for (unsigned i = 0; i < 8; ++i) record_offset |= static_cast<std::uint64_t>(bytes[locator + 8 + i]) << (8 * i);

    for (std::uint64_t count : {std::uint64_t{2}, std::uint64_t{1} << 40, std::uint64_t{0x0FFFFFFFFFFFFFFF}, UINT64_MAX}) {
        CAPTURE(count);
        Bytes huge = bytes;
        for (unsigned i = 0; i < 8; ++i) {
            huge[record_offset + 24 + i] = static_cast<std::uint8_t>(count >> (8 * i));
            huge[record_offset + 32 + i] = static_cast<std::uint8_t>(count >> (8 * i));
        }
        const auto result = open_bytes(huge);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::ZipOpenFailed);
    }
}

TEST_CASE("zip: a central directory that claims more entries than it holds") {
    ZipSpec spec;
    for (int i = 0; i < 3; ++i) spec.entries.push_back(file("e" + std::to_string(i)));
    Bytes bytes = build_zip(spec);
    const std::size_t eocd = bytes.size() - 22;
    bytes[eocd + 8] = 200;
    bytes[eocd + 10] = 200;
    const auto result = open_bytes(bytes);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("zip: a comment at the end is allowed") {
    ZipSpec spec = single(file("a.txt"));
    spec.comment = "created by a test";
    CHECK(open_ok(spec).entries().size() == 1);

    ZipSpec fake_eocd = single(file("a.txt"));
    fake_eocd.comment = std::string("PK\x05\x06", 4) + std::string(20, 'x');
    CHECK(open_ok(fake_eocd).entries().size() == 1);
}

TEST_CASE("zip: damaged central directories never crash") {
    ZipSpec spec;
    spec.entries.push_back(file("manifest.json", "{}"));
    spec.entries.push_back(file("pages/p00001/meta.json", std::string(200, 'z'), 8));
    spec.entries.push_back(file("pages/p00001/strokes.bin", std::string(50, 'q')));
    const Bytes valid = build_zip(spec);
    REQUIRE(open_bytes(valid).has_value());

    std::mt19937_64 rng(4242);
    for (int round = 0; round < 1500; ++round) {
        Bytes mutated = valid;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int f = 0; f < flips; ++f) mutated[rng() % mutated.size()] = static_cast<std::uint8_t>(rng());
        auto result = open_bytes(std::move(mutated));
        if (result.has_value()) {
            for (std::size_t i = 0; i < result.value().entries().size(); ++i) static_cast<void>(result.value().read(i));
        }
    }
}

TEST_CASE("byte source: memory") {
    const ByteSource source = ByteSource::from_memory({1, 2, 3, 4, 5});
    CHECK(source.size() == 5);
    CHECK(source.file_path() == nullptr);

    std::vector<std::uint8_t> out(3);
    REQUIRE(source.read_at(1, out).has_value());
    CHECK(out == std::vector<std::uint8_t>{2, 3, 4});
    CHECK(source.read_at(2, out).has_value());
    CHECK_FALSE(source.read_at(3, out).has_value());
    CHECK_FALSE(source.read_at(6, out).has_value());
    CHECK_FALSE(source.read_at(UINT64_MAX, out).has_value());
    CHECK(source.read_at(5, std::span<std::uint8_t>{}).has_value());
    CHECK_FALSE(source.read_at(6, std::span<std::uint8_t>{}).has_value());
}

TEST_CASE("byte source: file") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "onf_tools_test_archive.onf";
    ZipSpec spec;
    spec.entries.push_back(file("manifest.json", "{\"a\":1}"));
    spec.entries.push_back(file("pages/p00001/meta.json", std::string(500, 'm'), 8));
    const Bytes bytes = build_zip(spec);
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    auto source = ByteSource::from_file(path);
    REQUIRE(source.has_value());
    CHECK(source.value().size() == bytes.size());
    CHECK(source.value().file_path() != nullptr);

    std::vector<std::uint8_t> head(4);
    REQUIRE(source.value().read_at(0, head).has_value());
    CHECK(head == std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 4));
    CHECK_FALSE(source.value().read_at(bytes.size() - 2, head).has_value());

    auto archive = ZipArchive::open(std::move(source).value());
    REQUIRE(archive.has_value());
    CHECK(archive.value().entries().size() == 2);
    CHECK(archive.value().read(0).value() == text("{\"a\":1}"));
    CHECK(archive.value().read(1).value() == text(std::string(500, 'm')));

    std::filesystem::remove(path);
}

TEST_CASE("byte source: a missing file is an error, not a crash") {
    const auto source = ByteSource::from_file("/this/path/does/not/exist.onf");
    REQUIRE_FALSE(source.has_value());
    CHECK(source.error().code == ErrorCode::ZipOpenFailed);
}
