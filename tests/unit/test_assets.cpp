#include "doctest.h"

#include <array>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/primitives/sha256.h"
#include "onf/read/asset_check.h"
#include "onf/read/document.h"
#include "support/zip_builder.h"

using namespace onf;
using namespace onf::test;

namespace {

void append_be32(Bytes& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
}

void append_be16(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void append_text(Bytes& out, const char* text) {
    for (const char* c = text; *c != 0; ++c) out.push_back(static_cast<std::uint8_t>(*c));
}

Bytes png(std::uint32_t width, std::uint32_t height) {
    Bytes out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    append_be32(out, 13);
    append_text(out, "IHDR");
    append_be32(out, width);
    append_be32(out, height);
    for (const int b : {8, 2, 0, 0, 0}) out.push_back(static_cast<std::uint8_t>(b));
    append_be32(out, 0);  // crc, not checked
    return out;
}

// JPEG: SOI, an APP0 segment, then a start-of-frame segment with the given marker.
Bytes jpeg(std::uint32_t width, std::uint32_t height, std::uint8_t sof = 0xC0) {
    Bytes out = {0xFF, 0xD8, 0xFF, 0xE0};
    append_be16(out, 16);
    append_text(out, "JFIF");
    out.push_back(0);
    for (int i = 0; i < 9; ++i) out.push_back(0);
    out.push_back(0xFF);
    out.push_back(sof);
    append_be16(out, 11);
    out.push_back(8);
    append_be16(out, height);
    append_be16(out, width);
    for (const int b : {1, 1, 0x11, 0}) out.push_back(static_cast<std::uint8_t>(b));
    return out;
}

Bytes webp_lossy(std::uint32_t width, std::uint32_t height) {
    Bytes out;
    append_text(out, "RIFF");
    append_be32(out, 0);
    append_text(out, "WEBPVP8 ");
    append_be32(out, 0);
    for (const int b : {0x10, 0x00, 0x00, 0x9D, 0x01, 0x2A}) out.push_back(static_cast<std::uint8_t>(b));
    out.push_back(static_cast<std::uint8_t>(width));
    out.push_back(static_cast<std::uint8_t>(width >> 8));
    out.push_back(static_cast<std::uint8_t>(height));
    out.push_back(static_cast<std::uint8_t>(height >> 8));
    return out;
}

Bytes webp_lossless(std::uint32_t width, std::uint32_t height) {
    Bytes out;
    append_text(out, "RIFF");
    append_be32(out, 0);
    append_text(out, "WEBPVP8L");
    append_be32(out, 0);
    out.push_back(0x2F);
    const std::uint32_t bits = (width - 1) | ((height - 1) << 14);
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(bits >> shift));
    return out;
}

Bytes webp_extended(std::uint32_t width, std::uint32_t height) {
    Bytes out;
    append_text(out, "RIFF");
    append_be32(out, 0);
    append_text(out, "WEBPVP8X");
    append_be32(out, 10);
    for (int i = 0; i < 4; ++i) out.push_back(0);  // flags and reserved
    for (const std::uint32_t v : {width - 1, height - 1}) {
        for (int shift = 0; shift < 24; shift += 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
    return out;
}

AssetCheck check(const char* extension, const Bytes& bytes) { return check_asset_content(extension, bytes); }

}  // namespace

TEST_CASE("assets: the supported extensions are exactly those of spec 9.2.2") {
    for (const char* ok : {"webp", "png", "jpg", "pdf"}) CHECK(is_supported_asset_extension(ok));
    for (const char* bad : {"jpeg", "gif", "svg", "bmp", "heic", "avif", "PNG", "Png", "", "png ", ".png"}) {
        CAPTURE(bad);
        CHECK_FALSE(is_supported_asset_extension(bad));
    }
}

TEST_CASE("assets: PNG, JPEG and WebP headers give the pixel size") {
    struct Case { const char* extension; Bytes bytes; std::uint32_t width, height; };
    const std::vector<Case> cases = {
        {"png", png(640, 480), 640, 480},
        {"jpg", jpeg(1920, 1080), 1920, 1080},
        {"jpg", jpeg(100, 200, 0xC2), 100, 200},  // progressive
        {"webp", webp_lossy(321, 123), 321, 123},
        {"webp", webp_lossless(16384, 1), 16384, 1},
        {"webp", webp_extended(4000, 3000), 4000, 3000},
    };
    for (const Case& c : cases) {
        CAPTURE(c.extension);
        const AssetCheck result = check(c.extension, c.bytes);
        CHECK(result.status == AssetStatus::Ok);
        CHECK(result.width == c.width);
        CHECK(result.height == c.height);
    }
}

TEST_CASE("assets: the decoded size limit is 100000000 pixels (spec 9.2.3)") {
    CHECK(check("png", png(10000, 10000)).status == AssetStatus::Ok);
    CHECK(check("png", png(10001, 10000)).status == AssetStatus::Corrupt);
    CHECK(check("png", png(0xFFFFFFFF, 0xFFFFFFFF)).status == AssetStatus::Corrupt);  // no overflow
    CHECK(check("jpg", jpeg(65535, 65535)).status == AssetStatus::Corrupt);
    CHECK(check("jpg", jpeg(10000, 10000)).status == AssetStatus::Ok);
    CHECK(check("webp", webp_extended(16777216, 16777216)).status == AssetStatus::Corrupt);
    CHECK(check("png", png(0, 5)).status == AssetStatus::Corrupt);
    CHECK(check("png", png(5, 0)).status == AssetStatus::Corrupt);
}

TEST_CASE("assets: the leading bytes must match the extension") {
    const Bytes the_png = png(10, 10);
    const Bytes the_jpeg = jpeg(10, 10);
    const Bytes the_webp = webp_lossy(10, 10);
    const Bytes the_pdf = {'%', 'P', 'D', 'F', '-', '1', '.', '7'};

    CHECK(check("pdf", the_pdf).status == AssetStatus::Ok);
    for (const char* extension : {"png", "jpg", "webp", "pdf"}) {
        CAPTURE(extension);
        const Bytes* own = std::string(extension) == "png" ? &the_png : std::string(extension) == "jpg" ? &the_jpeg
                           : std::string(extension) == "webp" ? &the_webp : &the_pdf;
        for (const Bytes* other : {&the_png, &the_jpeg, &the_webp, &the_pdf}) {
            if (other == own) continue;
            CHECK(check(extension, *other).status == AssetStatus::Corrupt);
        }
        CHECK(check(extension, {}).status == AssetStatus::Corrupt);
    }
}

TEST_CASE("assets: truncated headers are corrupt, never an out-of-bounds read") {
    // The number of leading bytes each check needs to read the size (PNG: signature and IHDR, JPEG: up to the
    // dimensions in the frame header, WebP: RIFF header and the dimensions of the first chunk, PDF: the magic).
    struct Case { const char* extension; Bytes bytes; std::size_t needed; };
    const std::vector<Case> cases = {
        {"png", png(10, 10), 24}, {"jpg", jpeg(10, 10), 29}, {"webp", webp_lossy(10, 10), 30},
        {"webp", webp_lossless(10, 10), 25}, {"webp", webp_extended(10, 10), 30}, {"pdf", Bytes{'%', 'P', 'D', 'F', '-'}, 5},
    };
    for (const Case& c : cases) {
        CAPTURE(c.extension);
        REQUIRE(check(c.extension, c.bytes).status == AssetStatus::Ok);
        for (std::size_t size = 0; size <= c.bytes.size(); ++size) {
            CAPTURE(size);
            const AssetStatus status = check(c.extension, Bytes(c.bytes.begin(), c.bytes.begin() + static_cast<std::ptrdiff_t>(size))).status;
            CHECK(status == (size < c.needed ? AssetStatus::Corrupt : AssetStatus::Ok));
        }
    }
}

TEST_CASE("assets: an unsupported extension is not decoded") {
    const AssetCheck result = check("gif", Bytes{'G', 'I', 'F', '8', '9', 'a'});
    CHECK(result.status == AssetStatus::UnsupportedFormat);
}

TEST_CASE("assets: random bytes never crash the header checks") {
    std::mt19937_64 rng(99);
    for (int round = 0; round < 5000; ++round) {
        Bytes data(rng() % 80);
        for (auto& b : data) b = static_cast<std::uint8_t>(rng());
        // Give some of them a valid signature so the deeper paths are reached.
        if (round % 3 == 0 && data.size() > 12) { data[0] = 0x89; data[1] = 'P'; data[2] = 'N'; data[3] = 'G'; }
        if (round % 3 == 1 && data.size() > 12) { data[0] = 0xFF; data[1] = 0xD8; data[2] = 0xFF; }
        if (round % 3 == 2 && data.size() > 12) { data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F'; data[8] = 'W'; data[9] = 'E'; data[10] = 'B'; data[11] = 'P'; }
        for (const char* extension : {"png", "jpg", "webp", "pdf"}) static_cast<void>(check(extension, data));
    }
}

// ---------------------------------------------------------------------------
// Document::asset
// ---------------------------------------------------------------------------

namespace {

const std::string kMeta = R"({"width":21000,"height":29700,"background":{"type":"blank","color":"#FFFFFFFF"},)"
                          R"("text_objects":[],"assets":[]})";

Bytes text_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

ZipEntrySpec entry(std::string name, const Bytes& data) {
    ZipEntrySpec e;
    e.name = std::move(name);
    e.data = data;
    return e;
}

Document document_with(const std::vector<std::pair<std::string, Bytes>>& assets) {
    ZipSpec spec;
    spec.entries.push_back(entry("manifest.json", text_bytes(R"({"onf_version":"0.5.0","title":"T","created_at":"2026-06-14T10:32:00.000Z",)"
                                                             R"("modified_at":"2026-06-14T12:15:00.000Z","pages":[{"id":"p00001","order":1}]})")));
    spec.entries.push_back(entry("pages/p00001/meta.json", text_bytes(kMeta)));
    Bytes strokes = file_header();
    append(strokes, chunk(0x0002));
    spec.entries.push_back(entry("pages/p00001/strokes.bin", strokes));
    for (const auto& [name, data] : assets) spec.entries.push_back(entry("assets/" + name, data));

    auto inspection = inspect(ByteSource::from_memory(build_zip(spec)));
    REQUIRE(inspection.has_value());
    auto document = std::move(inspection).value().open();
    REQUIRE(document.has_value());
    return std::move(document).value();
}

AssetRef placement(const Bytes& data, const char* extension) {
    AssetRef ref;
    ref.id = "a1";
    ref.layer = 1;
    ref.hash = sha256_hex(data);
    ref.extension = extension;
    ref.width = 100;
    ref.height = 100;
    return ref;
}

}  // namespace

TEST_CASE("document: an asset with the right name and content is loaded") {
    const Bytes data = png(40, 30);
    const AssetRef ref = placement(data, "png");
    const Document document = document_with({{ref.hash + ".png", data}});

    CollectingSink sink;
    const auto result = document.asset(ref, "p00001", &sink);
    REQUIRE(result.has_value());
    CHECK(result.value().check.status == AssetStatus::Ok);
    CHECK(result.value().check.width == 40);
    CHECK(result.value().bytes == data);
    CHECK(sink.items().empty());
}

TEST_CASE("document: a missing asset is a status and an event, not an error") {
    const Bytes data = png(40, 30);
    const AssetRef ref = placement(data, "png");
    const Document document = document_with({});

    CollectingSink sink;
    const auto result = document.asset(ref, "p00001", &sink);
    REQUIRE(result.has_value());
    CHECK(result.value().check.status == AssetStatus::Missing);
    CHECK(result.value().bytes.empty());
    REQUIRE(sink.items().size() == 1);
    CHECK(sink.items()[0].kind == DiagnosticKind::MissingAsset);
    CHECK(sink.items()[0].page_id == "p00001");
    CHECK(sink.items()[0].detail.find(ref.hash) != std::string::npos);
}

TEST_CASE("document: an unsupported extension never reaches the archive path") {
    const Bytes data = {'G', 'I', 'F', '8'};
    AssetRef ref = placement(data, "gif");
    // Even a file that exists under this name is not read (spec 4.6.1).
    const Document document = document_with({{ref.hash + ".gif", data}});

    CollectingSink sink;
    const auto result = document.asset(ref, "p00001", &sink);
    REQUIRE(result.has_value());
    CHECK(result.value().check.status == AssetStatus::UnsupportedFormat);
    REQUIRE(sink.items().size() == 1);
    CHECK(sink.items()[0].kind == DiagnosticKind::UnsupportedAssetFormat);

    ref.extension = "../x";
    CHECK(document.asset(ref, "p00001").value().check.status == AssetStatus::UnsupportedFormat);
}

TEST_CASE("document: a wrong hash, wrong content and a too large image are corrupt") {
    const Bytes good = png(40, 30);
    const std::string name = sha256_hex(good);

    // The file is not what its name says.
    {
        AssetRef ref = placement(good, "png");
        const Document document = document_with({{name + ".png", png(41, 30)}});
        CollectingSink sink;
        const auto result = document.asset(ref, "p00001", &sink);
        REQUIRE(result.has_value());
        CHECK(result.value().check.status == AssetStatus::Corrupt);
        REQUIRE(sink.items().size() == 1);
        CHECK(sink.items()[0].kind == DiagnosticKind::CorruptAsset);

        // The check can be switched off for large assets (spec 9.2.1).
        AssetOptions options;
        options.verify_hash = false;
        CHECK(document.asset(ref, "p00001", nullptr, options).value().check.status == AssetStatus::Ok);
    }

    // The name is right but the content is not a PNG.
    {
        const Bytes fake = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
        const AssetRef ref = placement(fake, "png");
        const Document document = document_with({{ref.hash + ".png", fake}});
        CHECK(document.asset(ref, "p00001").value().check.status == AssetStatus::Corrupt);
    }

    // Too many pixels.
    {
        const Bytes huge = png(20000, 20000);
        const AssetRef ref = placement(huge, "png");
        const Document document = document_with({{ref.hash + ".png", huge}});
        CollectingSink sink;
        const auto result = document.asset(ref, "p00001", &sink);
        CHECK(result.value().check.status == AssetStatus::Corrupt);
        CHECK(result.value().bytes.empty());
    }
}

TEST_CASE("document: one PDF file serves many placements") {
    const Bytes pdf = {'%', 'P', 'D', 'F', '-', '1', '.', '7', '\n'};
    AssetRef first = placement(pdf, "pdf");
    first.pdf_page = 1;
    AssetRef second = first;
    second.id = "a2";
    second.pdf_page = 9;
    const Document document = document_with({{first.hash + ".pdf", pdf}});
    CHECK(document.asset(first, "p00001").value().check.status == AssetStatus::Ok);
    CHECK(document.asset(second, "p00001").value().bytes == pdf);
}

TEST_CASE("document: a crop region has to lie inside the image (spec 9.4.5)") {
    const Bytes data = png(40, 30);
    const Document document = document_with({{sha256_hex(data) + ".png", data}});

    const auto status_for = [&](Crop crop, DiagnosticsSink* sink = nullptr) {
        AssetRef ref = placement(data, "png");
        ref.crop = crop;
        const auto result = document.asset(ref, "p00001", sink);
        REQUIRE(result.has_value());
        return result.value().check.status;
    };

    // The whole image, one pixel in each corner, and a region in the middle.
    CHECK(status_for({0, 0, 40, 30}) == AssetStatus::Ok);
    CHECK(status_for({39, 29, 1, 1}) == AssetStatus::Ok);
    CHECK(status_for({0, 0, 1, 1}) == AssetStatus::Ok);
    CHECK(status_for({10, 5, 20, 20}) == AssetStatus::Ok);

    // One pixel too far on either side is a corrupt asset, and the host is told.
    CollectingSink sink;
    CHECK(status_for({1, 0, 40, 30}, &sink) == AssetStatus::Corrupt);
    REQUIRE(sink.items().size() == 1);
    CHECK(sink.items()[0].kind == DiagnosticKind::CorruptAsset);
    CHECK(sink.items()[0].detail.find("crop") != std::string::npos);
    CHECK(status_for({0, 1, 40, 30}) == AssetStatus::Corrupt);
    CHECK(status_for({0, 0, 41, 30}) == AssetStatus::Corrupt);
    CHECK(status_for({0, 0, 40, 31}) == AssetStatus::Corrupt);
    CHECK(status_for({40, 0, 1, 1}) == AssetStatus::Corrupt);

    // The sum of the largest values does not overflow.
    CHECK(status_for({2147483647, 2147483647, 2147483647, 2147483647}) == AssetStatus::Corrupt);

    // A corrupt asset gives no bytes; without a crop the same file is fine.
    AssetRef outside = placement(data, "png");
    outside.crop = Crop{0, 0, 41, 1};
    CHECK(document.asset(outside, "p00001").value().bytes.empty());
    CHECK(document.asset(placement(data, "png"), "p00001").value().bytes == data);
}

TEST_CASE("document: the pixel limit applies to the whole image, not to the region") {
    const Bytes huge = png(20000, 20000);
    const Document document = document_with({{sha256_hex(huge) + ".png", huge}});
    AssetRef ref = placement(huge, "png");
    ref.crop = Crop{0, 0, 10, 10};
    CHECK(document.asset(ref, "p00001").value().check.status == AssetStatus::Corrupt);
}
