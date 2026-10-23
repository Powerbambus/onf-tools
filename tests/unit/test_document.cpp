#include "doctest.h"

#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/limits.h"
#include "onf/read/document.h"
#include "onf/read/values.h"
#include "support/zip_builder.h"

using namespace onf;
using namespace onf::test;

#ifndef ONF_FIXTURES_DIR
#error "ONF_FIXTURES_DIR must point to tests/fixtures"
#endif

namespace {

const std::string kMeta = R"({"width":21000,"height":29700,"background":{"type":"blank","color":"#FFFFFFFF"},)"
                          R"("text_objects":[],"assets":[]})";

struct TestPage {
    std::string id;
    std::string meta = kMeta;
    Bytes strokes;
};

Bytes text_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes strokes_file(std::initializer_list<Bytes> stroke_bodies) {
    Bytes out = file_header();
    for (const Bytes& body : stroke_bodies) append(out, chunk(0x0001, body));
    append(out, chunk(0x0002));
    return out;
}

Bytes one_stroke(std::uint32_t id, std::int64_t x = 100) {
    StrokeHeaderIn header;
    header.stroke_id = id;
    return stroke_body(header, {SampleIn{x, 200, 255, 0}, SampleIn{x + 1, 201, 250, 5}});
}

std::string manifest_json(const std::string& version, const std::vector<std::pair<std::string, int>>& pages) {
    std::string list;
    for (const auto& [id, order] : pages) {
        if (!list.empty()) list += ",";
        list += "{\"id\":\"" + id + "\",\"order\":" + std::to_string(order) + "}";
    }
    return "{\"onf_version\":\"" + version + R"(","title":"Test","created_at":"2026-06-14T10:32:00.000Z",)"
           R"("modified_at":"2026-06-14T12:15:00.000Z","pages":[)" + list + "]}";
}

ZipEntrySpec entry(std::string name, const std::string& content) {
    ZipEntrySpec e;
    e.name = std::move(name);
    e.data = text_bytes(content);
    return e;
}

ZipEntrySpec entry(std::string name, const Bytes& content) {
    ZipEntrySpec e;
    e.name = std::move(name);
    e.data = content;
    return e;
}

ZipSpec onf_spec(const std::string& manifest, const std::vector<TestPage>& pages) {
    ZipSpec spec;
    spec.entries.push_back(entry("manifest.json", manifest));
    for (const TestPage& page : pages) {
        spec.entries.push_back(entry("pages/" + page.id + "/meta.json", page.meta));
        spec.entries.push_back(entry("pages/" + page.id + "/strokes.bin", page.strokes));
    }
    return spec;
}

ZipSpec three_pages() {
    return onf_spec(manifest_json("0.5.0", {{"p00001", 1}, {"p00002", 2}, {"p00003", 3}}),
                    {{"p00001", kMeta, strokes_file({one_stroke(1), one_stroke(2)})},
                     {"p00002", kMeta, strokes_file({one_stroke(1)})},
                     {"p00003", kMeta, strokes_file({})}});
}

Result<Inspection, FileError> inspect_spec(const ZipSpec& spec, DiagnosticsSink* sink = nullptr) {
    return inspect(ByteSource::from_memory(build_zip(spec)), sink);
}

Document open_ok(const ZipSpec& spec, OpenDecision decision = {}) {
    auto inspection = inspect_spec(spec);
    REQUIRE_MESSAGE(inspection.has_value(), "inspect failed: " << inspection.error().message);
    auto document = std::move(inspection).value().open(decision);
    REQUIRE_MESSAGE(document.has_value(), "open failed: " << document.error().message);
    return std::move(document).value();
}

ErrorCode inspect_error(const ZipSpec& spec) {
    const auto result = inspect_spec(spec);
    REQUIRE_MESSAGE(!result.has_value(), "inspect unexpectedly worked");
    return result.error().code;
}

ErrorCode open_error(const ZipSpec& spec, OpenDecision decision = {}) {
    auto inspection = inspect_spec(spec);
    REQUIRE_MESSAGE(inspection.has_value(), "inspect failed: " << inspection.error().message);
    const auto result = std::move(inspection).value().open(decision);
    REQUIRE_MESSAGE(!result.has_value(), "open unexpectedly worked");
    return result.error().code;
}

Bytes read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string fixture(const char* name) { return std::string(ONF_FIXTURES_DIR) + "/valid/" + name; }

}  // namespace

TEST_CASE("document: a file written by an independent encoder") {
    auto source = ByteSource::from_file(fixture("minimal.onf"));
    REQUIRE(source.has_value());

    CollectingSink sink;
    auto inspection = inspect(std::move(source).value(), &sink);
    REQUIRE(inspection.has_value());
    CHECK(inspection.value().version() == Version{0, 5, 0});
    CHECK(inspection.value().status() == VersionStatus::Supported);
    CHECK(sink.items().empty());

    auto opened = std::move(inspection).value().open();
    REQUIRE(opened.has_value());
    const Document& document = opened.value();
    CHECK(document.manifest().title == "Minimal");
    CHECK(document.manifest().generator == "make_valid_onf.py");

    REQUIRE(document.pages().size() == 2);
    CHECK(document.pages()[0].id == "p00002");
    CHECK(document.pages()[1].id == "p00001");

    const auto first = document.page("p00001", &sink);
    REQUIRE_MESSAGE(first.has_value(), first.error().message);
    CHECK(first.value().id == "p00001");
    CHECK(first.value().meta.width == 21000);
    CHECK(first.value().meta.height == 29700);
    CHECK(first.value().meta.background.kind == BackgroundKind::Dotted);
    CHECK(first.value().meta.background.spacing == 500);
    REQUIRE(first.value().ink.size() == 3);

    const Stroke& stroke = std::get<Stroke>(first.value().ink[0]);
    CHECK(stroke.stroke_id == 1);
    CHECK(stroke.created_at == 1781433600000ull);
    CHECK(stroke.color == Color{10, 20, 30, 255});
    CHECK(stroke.tool == ToolType::Fountain);
    CHECK(stroke.base_width == 100);
    CHECK(stroke.samples.x == std::vector<Coord>{10000, 10004, 10009});
    CHECK(stroke.samples.y == std::vector<Coord>{5000, 5003, 5007});
    CHECK(stroke.samples.pressure == std::vector<std::uint8_t>{200, 202, 200});
    CHECK(stroke.samples.timestamp == std::vector<Timestamp>{0, 8, 16});
    CHECK(stroke.layer == 2);

    // Vector V1 of spec Appendix C: no timestamps and no created_at.
    const Stroke& no_time = std::get<Stroke>(first.value().ink[1]);
    CHECK(no_time.stroke_id == 2);
    CHECK_FALSE(no_time.created_at.has_value());
    CHECK(no_time.flags.no_timestamp());
    CHECK(no_time.samples.timestamp.empty());
    CHECK(no_time.samples.x == std::vector<Coord>{10000, 10004});
    CHECK(no_time.samples.pressure == std::vector<std::uint8_t>{255, 255});

    // Vector V3: the ellipse follows the strokes in file order.
    const Shape& shape = std::get<Shape>(first.value().ink[2]);
    CHECK(shape.stroke_id == 3);
    CHECK(shape.layer == 2);
    const Ellipse& ellipse = std::get<Ellipse>(shape.geometry);
    CHECK(ellipse.cx == 10500);
    CHECK(ellipse.ry == 1500);
    CHECK(ellipse.rotation == 4500);

    REQUIRE(first.value().meta.text_objects.size() == 1);
    const TextObject& text = first.value().meta.text_objects[0];
    CHECK(text.layer == 3);
    CHECK(text.created_at == 1781433600000ull);
    REQUIRE(text.spans.size() == 2);
    CHECK(text.spans[0].bold == true);
    CHECK(text.spans[1].color == Color{255, 0, 0, 255});

    REQUIRE(first.value().meta.assets.size() == 1);
    CHECK(first.value().meta.assets[0].extension == "pdf");
    CHECK(first.value().meta.assets[0].pdf_page == 3);

    const auto second = document.page("p00002", &sink);
    REQUIRE(second.has_value());
    CHECK(second.value().meta.width == 14800);
    CHECK(second.value().meta.background.kind == BackgroundKind::Ruled);
    CHECK(second.value().ink.empty());
    CHECK(sink.items().empty());

    const auto thumbnail = document.thumbnail_bytes();
    REQUIRE(thumbnail.has_value());
    REQUIRE(thumbnail.value().has_value());
    CHECK(*thumbnail.value() == text_bytes("PNGDATA"));
}

TEST_CASE("document: inspect and open with a supported version") {
    CollectingSink sink;
    auto inspection = inspect_spec(three_pages(), &sink);
    REQUIRE(inspection.has_value());
    CHECK(inspection.value().status() == VersionStatus::Supported);
    CHECK(inspection.value().version() == Version{0, 5, 0});
    CHECK(sink.items().empty());

    auto document = std::move(inspection).value().open();
    REQUIRE(document.has_value());
    CHECK(document.value().pages().size() == 3);
    CHECK(document.value().manifest().onf_version == Version{0, 5, 0});
}

TEST_CASE("document: pages come back in display order with their content") {
    const Document document = open_ok(onf_spec(manifest_json("0.5.0", {{"p00001", 2}, {"p00002", 1}}),
                                               {{"p00001", kMeta, strokes_file({one_stroke(7, 11)})},
                                                {"p00002", kMeta, strokes_file({one_stroke(8, 22), one_stroke(9, 33)})}}));
    REQUIRE(document.pages().size() == 2);
    CHECK(document.pages()[0].id == "p00002");

    const auto first = document.page(document.pages()[0].id);
    REQUIRE(first.has_value());
    REQUIRE(first.value().ink.size() == 2);
    CHECK(std::get<Stroke>(first.value().ink[0]).stroke_id == 8);
    CHECK(std::get<Stroke>(first.value().ink[1]).samples.x[0] == 33);

    const auto second = document.page(document.pages()[1].id);
    REQUIRE(second.has_value());
    CHECK(std::get<Stroke>(second.value().ink[0]).stroke_id == 7);
}

TEST_CASE("document: a page can be read more than once") {
    const Document document = open_ok(three_pages());
    const auto a = document.page("p00001");
    const auto b = document.page("p00001");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a.value().ink.size() == b.value().ink.size());
    CHECK(std::get<Stroke>(a.value().ink[0]).samples.x == std::get<Stroke>(b.value().ink[0]).samples.x);
}

TEST_CASE("document: the document can be moved") {
    Document first = open_ok(three_pages());
    Document second = std::move(first);
    CHECK(second.page("p00002").has_value());
}

TEST_CASE("version: other 0.x versions need the decision of the host") {
    for (const char* version : {"0.1.4", "0.2.0", "0.0.1"}) {
        CAPTURE(version);
        const ZipSpec spec = onf_spec(manifest_json(version, {{"p00001", 1}}), {{"p00001", kMeta, strokes_file({})}});

        CollectingSink sink;
        auto inspection = inspect_spec(spec, &sink);
        REQUIRE(inspection.has_value());
        CHECK(inspection.value().status() == VersionStatus::UnsupportedPreRelease);
        REQUIRE(sink.items().size() == 1);
        CHECK(sink.items()[0].kind == DiagnosticKind::UnsupportedVersion);

        CHECK(open_error(spec) == ErrorCode::VersionRejected);
        CHECK(open_error(spec, OpenDecision{false}) == ErrorCode::VersionRejected);
        CHECK(open_ok(spec, OpenDecision{true}).pages().size() == 1);
    }
}

TEST_CASE("version: a newer major version needs the decision of the host") {
    for (const char* version : {"1.0.0", "2.7.1"}) {
        CAPTURE(version);
        const ZipSpec spec = onf_spec(manifest_json(version, {{"p00001", 1}}), {{"p00001", kMeta, strokes_file({})}});

        CollectingSink sink;
        auto inspection = inspect_spec(spec, &sink);
        REQUIRE(inspection.has_value());
        CHECK(inspection.value().status() == VersionStatus::NewerMajor);
        REQUIRE(sink.items().size() == 1);
        CHECK(sink.items()[0].kind == DiagnosticKind::MajorVersionMismatch);

        CHECK(open_error(spec) == ErrorCode::VersionRejected);
        CHECK(open_ok(spec, OpenDecision{true}).manifest().onf_version == *parse_version(version));
    }
}

TEST_CASE("version: the decision comes before the structure of a manifest from a newer format is checked") {
    ZipSpec spec;
    spec.entries.push_back(entry("manifest.json", R"({"onf_version":"3.0.0","pages":"a structure from the future"})"));

    CollectingSink sink;
    auto inspection = inspect_spec(spec, &sink);
    REQUIRE(inspection.has_value());
    CHECK(inspection.value().status() == VersionStatus::NewerMajor);
    CHECK(sink.items().size() == 1);

    CHECK(open_error(spec) == ErrorCode::VersionRejected);
    CHECK(open_error(spec, OpenDecision{true}) == ErrorCode::ManifestInvalidField);
}

TEST_CASE("inspect: container and manifest errors") {
    CHECK_FALSE(inspect(ByteSource::from_memory(text_bytes("not a zip"))).has_value());
    CHECK(inspect(ByteSource::from_memory({})).error().code == ErrorCode::ZipOpenFailed);

    ZipSpec no_manifest;
    no_manifest.entries.push_back(entry("pages/p00001/meta.json", kMeta));
    CHECK(inspect_error(no_manifest) == ErrorCode::ManifestMissing);

    ZipSpec bad_json;
    bad_json.entries.push_back(entry("manifest.json", "{"));
    CHECK(inspect_error(bad_json) == ErrorCode::JsonSyntax);

    ZipSpec no_version;
    no_version.entries.push_back(entry("manifest.json", R"({"title":"x"})"));
    CHECK(inspect_error(no_version) == ErrorCode::ManifestInvalidField);

    ZipSpec bad_version;
    bad_version.entries.push_back(entry("manifest.json", R"({"onf_version":"1.0"})"));
    CHECK(inspect_error(bad_version) == ErrorCode::ManifestInvalidField);

    ZipSpec encrypted = three_pages();
    encrypted.entries.push_back(entry("secret.bin", "x"));
    encrypted.entries.back().flags = 1;
    encrypted.entries.back().extra_payload_bytes = 12;
    CHECK(inspect_error(encrypted) == ErrorCode::ZipEncryptedEntry);
}

TEST_CASE("open: manifest errors reject the whole file") {
    CHECK(open_error(onf_spec(manifest_json("0.5.0", {}), {})) == ErrorCode::ManifestPagesInvalid);
    CHECK(open_error(onf_spec(manifest_json("0.5.0", {{"p00001", 1}, {"p00001", 2}}), {})) == ErrorCode::ManifestPagesInvalid);
    CHECK(open_error(onf_spec(manifest_json("0.5.0", {{"p00001", 1}, {"p00002", 2}}),
                              {{"p00001", kMeta, strokes_file({})}})) == ErrorCode::PageDirectoryMissing);
}

TEST_CASE("page: a broken page does not affect the others") {
    ZipSpec spec = three_pages();
    spec.entries[4].data = filler(30);

    CollectingSink sink;
    const Document document = open_ok(spec);

    const auto bad = document.page("p00002", &sink);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::StrokesBadMagic);
    CHECK(bad.error().page_id == "p00002");
    REQUIRE(sink.items().size() == 1);
    CHECK(sink.items()[0].kind == DiagnosticKind::PageError);
    CHECK(sink.items()[0].page_id == "p00002");

    CHECK(document.page("p00001", &sink).has_value());
    CHECK(document.page("p00003", &sink).has_value());
    CHECK(sink.items().size() == 1);
}

TEST_CASE("page: errors in the files of a page") {
    struct Case {
        const char* what;
        TestPage page;
        ErrorCode code;
    };
    std::vector<Case> cases;
    cases.push_back({"invalid strokes magic", {"p00001", kMeta, filler(40)}, ErrorCode::StrokesBadMagic});
    cases.push_back({"strokes version 0", {"p00001", kMeta, concat({file_header(0), chunk(2)})}, ErrorCode::StrokesVersionZero});
    cases.push_back({"duplicate stroke id", {"p00001", kMeta, strokes_file({one_stroke(5), one_stroke(5)})}, ErrorCode::StrokeIdDuplicate});
    cases.push_back({"truncated chunk", {"p00001", kMeta, concat({file_header(), filler(3)})}, ErrorCode::ChunkHeaderTruncated});
    cases.push_back({"pressure out of range", {"p00001", kMeta, strokes_file({[] {
                                                                              StrokeHeaderIn h;
                                                                              return stroke_body(h, {SampleIn{0, 0, 300}});
                                                                          }()})},
                     ErrorCode::SampleValueOutOfRange});
    cases.push_back({"meta is not JSON", {"p00001", "{", strokes_file({})}, ErrorCode::JsonSyntax});
    cases.push_back({"meta without size", {"p00001", R"({"background":{"type":"blank","color":"#FFFFFFFF"},"text_objects":[],"assets":[]})", strokes_file({})},
                     ErrorCode::MetaInvalidField});
    cases.push_back({"meta with a broken background",
                     {"p00001", R"({"width":1,"height":1,"background":{"type":"ruled","color":"#FFFFFFFF"},"text_objects":[],"assets":[]})", strokes_file({})},
                     ErrorCode::BackgroundInvalidField});

    for (const Case& c : cases) {
        CAPTURE(c.what);
        const Document document = open_ok(onf_spec(manifest_json("0.5.0", {{"p00001", 1}}), {c.page}));
        CollectingSink sink;
        const auto result = document.page("p00001", &sink);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == c.code);
        CHECK(result.error().page_id == "p00001");
        CHECK(sink.items().back().kind == DiagnosticKind::PageError);
    }
}

TEST_CASE("page: missing files of a page are page errors, not file errors") {
    ZipSpec no_strokes;
    no_strokes.entries.push_back(entry("manifest.json", manifest_json("0.5.0", {{"p00001", 1}, {"p00002", 2}})));
    no_strokes.entries.push_back(entry("pages/p00001/meta.json", kMeta));
    no_strokes.entries.push_back(entry("pages/p00002/meta.json", kMeta));
    no_strokes.entries.push_back(entry("pages/p00002/strokes.bin", strokes_file({})));

    const Document document = open_ok(no_strokes);
    const auto missing_strokes = document.page("p00001");
    REQUIRE_FALSE(missing_strokes.has_value());
    CHECK(missing_strokes.error().code == ErrorCode::PageFileMissing);
    CHECK(document.page("p00002").has_value());

    ZipSpec no_meta;
    no_meta.entries.push_back(entry("manifest.json", manifest_json("0.5.0", {{"p00001", 1}})));
    no_meta.entries.push_back(entry("pages/p00001/strokes.bin", strokes_file({})));
    CHECK(open_ok(no_meta).page("p00001").error().code == ErrorCode::PageFileMissing);
}

TEST_CASE("page: a strokes version other than 2 is reported and the page is unreadable") {
    const Document document = open_ok(onf_spec(manifest_json("0.5.0", {{"p00001", 1}}),
                                               {{"p00001", kMeta, concat({file_header(1), chunk(2)})}}));
    CollectingSink sink;
    const auto result = document.page("p00001", &sink);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::StrokesVersionUnsupported);
    REQUIRE(sink.items().size() == 2);
    CHECK(sink.items()[0].kind == DiagnosticKind::StrokesVersionMismatch);
    CHECK(sink.items()[1].kind == DiagnosticKind::PageError);
}

TEST_CASE("page: only pages of the manifest can be read") {
    ZipSpec spec = three_pages();
    spec.entries.push_back(entry("pages/p00009/meta.json", kMeta));
    spec.entries.push_back(entry("pages/p00009/strokes.bin", strokes_file({})));

    const Document document = open_ok(spec);
    CHECK(document.pages().size() == 3);
    for (const char* id : {"p00009", "p00004", "", "p0001", "../p00001", "pages/p00001"}) {
        CAPTURE(id);
        const auto result = document.page(id);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::PageDirectoryMissing);
    }
}

TEST_CASE("open: unknown entries are ignored") {
    ZipSpec spec = three_pages();
    spec.entries.push_back(entry("assets/", ""));
    spec.entries.push_back(entry("future/feature.dat", "something new"));
    spec.entries.push_back(entry("pages/p00001/notes.txt", "unknown file inside a page"));
    CHECK(open_ok(spec).page("p00001").has_value());
}

TEST_CASE("thumbnail") {
    CHECK(open_ok(three_pages()).thumbnail_bytes().value() == std::nullopt);

    ZipSpec spec = three_pages();
    spec.entries.push_back(entry("thumbnail.png", "not really a png"));
    const auto thumbnail = open_ok(spec).thumbnail_bytes();
    REQUIRE(thumbnail.has_value());
    REQUIRE(thumbnail.value().has_value());
    CHECK(*thumbnail.value() == text_bytes("not really a png"));

    ZipSpec nested = three_pages();
    nested.entries.push_back(entry("pages/thumbnail.png", "x"));
    nested.entries.push_back(entry("Thumbnail.png", "x"));
    CHECK(open_ok(nested).thumbnail_bytes().value() == std::nullopt);
}

TEST_CASE("thumbnail: exactly 16 MiB is read") {
    std::mt19937_64 rng(77);
    Bytes block(64 * 1024);
    for (std::uint8_t& byte : block) byte = static_cast<std::uint8_t>(rng());
    Bytes data;
    data.reserve(limits::kMaxThumbnailSize);
    while (data.size() < limits::kMaxThumbnailSize) data.insert(data.end(), block.begin(), block.end());
    data.resize(limits::kMaxThumbnailSize);

    ZipSpec spec = three_pages();
    spec.entries.push_back(entry("thumbnail.png", data));

    const auto thumbnail = open_ok(spec).thumbnail_bytes();
    REQUIRE(thumbnail.has_value());
    REQUIRE(thumbnail.value().has_value());
    CHECK(*thumbnail.value() == data);
}

TEST_CASE("thumbnail: larger than 16 MiB is refused without reading it") {
    ZipSpec spec = three_pages();
    ZipEntrySpec big = entry("thumbnail.png", "x");
    big.method = 8;
    big.declared_size = limits::kMaxThumbnailSize + 1;
    big.extra_payload_bytes = (*big.declared_size + 99) / 100 - deflate_raw(big.data).size();
    spec.entries.push_back(big);

    const auto result = open_ok(spec).thumbnail_bytes();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::ThumbnailTooLarge);
}

TEST_CASE("document: damaged files never crash any step") {
    const Bytes valid = read_file(fixture("minimal.onf"));
    REQUIRE(valid.size() > 1000);

    std::mt19937_64 rng(2025);
    for (int round = 0; round < 1200; ++round) {
        Bytes mutated = valid;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int f = 0; f < flips; ++f) mutated[rng() % mutated.size()] = static_cast<std::uint8_t>(rng());
        if (rng() % 5 == 0) mutated.resize(rng() % (mutated.size() + 1));

        CollectingSink sink;
        auto inspection = inspect(ByteSource::from_memory(std::move(mutated)), &sink);
        if (!inspection) continue;
        auto document = std::move(inspection).value().open(OpenDecision{true});
        if (!document) continue;
        for (const PageEntry& page : document.value().pages()) static_cast<void>(document.value().page(page.id, &sink));
        static_cast<void>(document.value().thumbnail_bytes());
    }
}
