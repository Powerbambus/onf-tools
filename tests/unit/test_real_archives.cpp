#include "doctest.h"

#include <map>
#include <string>
#include <vector>

#include "onf/container/zip_archive.h"

#ifndef ONF_FIXTURES_DIR
#error "ONF_FIXTURES_DIR must point to tests/fixtures"
#endif

using namespace onf;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes bytes_of(const std::string& text) { return Bytes(text.begin(), text.end()); }

// The files that tests/fixtures/make_real_archives.sh puts into every archive.
std::map<std::string, Bytes> expected_files() {
    std::string meta;
    for (int i = 0; i < 500; ++i) meta += "0123456789";
    Bytes strokes;
    for (int i = 0; i < 256; ++i) strokes.push_back(static_cast<std::uint8_t>(i));
    return {{"manifest.json", bytes_of("{\"hello\":\"world\"}\n")},
            {"pages/p00001/meta.json", bytes_of(meta)},
            {"pages/p00001/strokes.bin", strokes},
            {"thumbnail.png", bytes_of("PNGDATA")}};
}

}  // namespace

TEST_CASE("archives written by other tools open and read back identically") {
    const std::map<std::string, Bytes> expected = expected_files();

    for (const char* name : {"infozip_dirs.zip", "infozip_nodirs.zip", "7z.zip", "python.zip", "python_zip64.zip"}) {
        CAPTURE(name);
        auto source = ByteSource::from_file(std::string(ONF_FIXTURES_DIR) + "/real_archives/" + name);
        REQUIRE(source.has_value());
        auto archive = ZipArchive::open(std::move(source).value());
        REQUIRE_MESSAGE(archive.has_value(), error_name(archive.error().code) << ": " << archive.error().message);

        std::size_t files = 0;
        for (std::size_t i = 0; i < archive.value().entries().size(); ++i) {
            const ZipEntryInfo& entry = archive.value().entries()[i];
            CAPTURE(entry.name);
            if (entry.is_directory) continue;

            ++files;
            const auto want = expected.find(entry.name);
            REQUIRE(want != expected.end());
            CHECK(archive.value().read(i).value() == want->second);
        }
        CHECK(files == expected.size());
    }
}

TEST_CASE("directory entries are optional") {
    auto with_dirs = ZipArchive::open(ByteSource::from_file(std::string(ONF_FIXTURES_DIR) + "/real_archives/infozip_dirs.zip").value());
    auto without = ZipArchive::open(ByteSource::from_file(std::string(ONF_FIXTURES_DIR) + "/real_archives/infozip_nodirs.zip").value());
    REQUIRE(with_dirs.has_value());
    REQUIRE(without.has_value());
    CHECK(with_dirs.value().find("assets/").has_value());
    CHECK_FALSE(without.value().find("assets/").has_value());
}
