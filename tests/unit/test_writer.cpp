#include "doctest.h"

#include <unistd.h>

#include <array>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/container/zip_archive.h"
#include "onf/primitives/sha256.h"
#include "onf/read/document.h"
#include "onf/write/writer.h"
#include "support/strokes_builder.h"
#include "support/vectors.h"

using namespace onf;
using namespace onf::test;

namespace {

namespace fs = std::filesystem;

// A fresh directory for the files of one test case, removed afterwards.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        path_ = fs::temp_directory_path() / ("onf_writer_test_" + std::to_string(::getpid()) + "_" + std::to_string(++counter));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    fs::path file(const std::string& name) const { return path_ / name; }
    std::size_t file_count() const {
        std::size_t n = 0;
        for (const auto& entry : fs::directory_iterator(path_)) { static_cast<void>(entry); ++n; }
        return n;
    }

private:
    fs::path path_;
};

UtcDateTime time_of(unsigned year, unsigned month, unsigned day, unsigned hour = 10, unsigned minute = 32, unsigned second = 0, unsigned ms = 0) {
    UtcDateTime t;
    t.year = static_cast<std::uint16_t>(year);
    t.month = static_cast<std::uint8_t>(month);
    t.day = static_cast<std::uint8_t>(day);
    t.hour = static_cast<std::uint8_t>(hour);
    t.minute = static_cast<std::uint8_t>(minute);
    t.second = static_cast<std::uint8_t>(second);
    t.millisecond = static_cast<std::uint16_t>(ms);
    return t;
}

Stroke make_stroke(std::uint32_t id, std::uint16_t layer, std::vector<std::array<std::int32_t, 3>> points, bool timestamps) {
    Stroke s;
    s.stroke_id = id;
    s.layer = layer;
    s.color = {10, 20, 30, 255};
    s.tool_type_raw = 1;
    s.tool = ToolType::Fountain;
    s.base_width = 100;
    std::uint16_t flags = timestamps ? 0 : 1u << 3;
    s.flags = StrokeFlags{flags};
    std::uint32_t t = 0;
    for (const auto& p : points) {
        s.samples.x.push_back(p[0]);
        s.samples.y.push_back(p[1]);
        s.samples.pressure.push_back(static_cast<std::uint8_t>(p[2]));
        if (timestamps) {
            s.samples.timestamp.push_back(t);
            t += 8;
        }
    }
    return s;
}

Shape make_ellipse(std::uint32_t id, std::uint16_t layer) {
    Shape shape;
    shape.stroke_id = id;
    shape.layer = layer;
    shape.color = {255, 0, 0, 255};
    shape.base_width = 100;
    shape.geometry = Ellipse{10500, 14850, 3000, 1500, 4500};
    return shape;
}

TextObject make_text(const std::string& id, std::uint16_t layer) {
    TextObject t;
    t.id = id;
    t.layer = layer;
    t.x = 100;
    t.y = 200;
    t.width = 8000;
    t.height = 4000;
    t.content = "Hello \xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80 \"quoted\" back\\slash\nnext line\ttab";
    t.font_family = "Helvetica";
    t.generic_family = "sans-serif";
    t.font_size = 423;
    t.color = {0, 0, 0, 255};
    return t;
}

Bytes make_png(std::uint32_t width, std::uint32_t height) {
    Bytes out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (const int b : std::initializer_list<int>{0, 0, 0, 13, 'I', 'H', 'D', 'R'}) out.push_back(static_cast<std::uint8_t>(b));
    for (const std::uint32_t v : {width, height}) {
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
    for (const int b : {8, 2, 0, 0, 0, 0, 0, 0, 0}) out.push_back(static_cast<std::uint8_t>(b));
    return out;
}

WriteDocument make_document() {
    WriteDocument d;
    d.title = "Round trip \xC3\xA4";
    d.generator = "onf-tools tests";
    d.created_at = time_of(2026, 6, 14, 10, 32, 0, 5);
    d.modified_at = time_of(2026, 6, 14, 12, 15, 7, 999);

    const Bytes png = make_png(40, 30);
    d.assets.push_back({"png", png});
    const Bytes pdf = {'%', 'P', 'D', 'F', '-', '1', '.', '7', '\n'};
    d.assets.push_back({"pdf", pdf});

    WritePage first;
    first.meta.width = 21000;
    first.meta.height = 29700;
    first.meta.background.type = "dotted";
    first.meta.background.kind = BackgroundKind::Dotted;
    first.meta.background.color = {255, 255, 255, 255};
    first.meta.background.foreground_color = Color{0, 0, 0, 255};
    first.meta.background.spacing = 500;
    first.meta.background.top_margin = 1000;

    Stroke with_time = make_stroke(1, 2, {{10000, 5000, 200}, {10004, 5003, 202}, {10009, 5007, 200}}, true);
    with_time.created_at = 1781433600000ull;
    with_time.flags = StrokeFlags{static_cast<std::uint16_t>(with_time.flags.raw() | (1u << 0) | (1u << 1))};
    with_time.samples.tilt_x = {1, 2, -3};
    with_time.samples.tilt_y = {-4, 0, 90};
    with_time.samples.azimuth = {0, 35999, 100};
    first.ink.push_back(with_time);
    Stroke cut = make_stroke(2, 2, {{-500, -600, 255}, {2147483647, -2147483647 - 1, 255}}, false);
    cut.flags = StrokeFlags{static_cast<std::uint16_t>(cut.flags.raw() | (1u << 5) | (1u << 6))};
    first.ink.push_back(cut);
    first.ink.push_back(make_ellipse(3, 3));
    Shape line;
    line.stroke_id = 4;
    line.layer = 1;
    line.color = {1, 2, 3, 4};
    line.tool_type_raw = 3;
    line.tool = ToolType::Highlighter;
    line.base_width = 700;
    line.created_at = 7;
    line.geometry = Line{-500, 2000, 1000, -1000};
    first.ink.push_back(line);

    TextObject text = make_text("t1", 3);
    text.created_at = 1781433600000ull;
    Span bold;
    bold.start = 0;
    bold.end = 5;
    bold.bold = true;
    Span styled;
    styled.start = 6;
    styled.end = 9;
    styled.color = Color{255, 0, 0, 128};
    styled.font_size = 600;
    styled.font_family = "Mono";
    styled.generic_family = "monospace";
    styled.underline = true;
    styled.strikethrough = false;
    text.spans = {bold, styled};
    first.meta.text_objects.push_back(text);

    AssetRef image;
    image.id = "a1";
    image.layer = 1;
    image.hash = sha256_hex(png);
    image.extension = "png";
    image.x = 0;
    image.y = 0;
    image.width = 5000;
    image.height = 3000;
    image.crop = Crop{1, 2, 30, 20};  // inside the 40 x 30 image
    AssetRef page_of_pdf = image;
    page_of_pdf.crop.reset();
    page_of_pdf.id = "a2";
    page_of_pdf.hash = sha256_hex(pdf);
    page_of_pdf.extension = "pdf";
    page_of_pdf.pdf_page = 7;
    page_of_pdf.created_at = 1;
    first.meta.assets = {image, page_of_pdf};

    WritePage second;
    second.meta.width = 14800;
    second.meta.height = 21000;
    second.meta.background.type = "blank";
    second.meta.background.kind = BackgroundKind::Blank;
    second.meta.background.color = {250, 250, 240, 255};

    d.pages = {first, second};
    d.thumbnail = make_png(100, 141);
    return d;
}

Document open_file(const fs::path& path, DiagnosticsSink* sink = nullptr) {
    auto source = ByteSource::from_file(path);
    REQUIRE_MESSAGE(source.has_value(), source.error().message);
    auto inspection = inspect(std::move(source).value(), sink);
    REQUIRE_MESSAGE(inspection.has_value(), inspection.error().message);
    CHECK(inspection.value().status() == VersionStatus::Supported);
    auto document = std::move(inspection).value().open();
    REQUIRE_MESSAGE(document.has_value(), document.error().message);
    return std::move(document).value();
}

std::string problem_of(const WriteDocument& d) {
    const auto message = check_writable(d);
    REQUIRE_MESSAGE(message.has_value(), "the document was accepted");
    return *message;
}

}  // namespace

TEST_CASE("writer: a document survives a round trip through a file") {
    TempDir dir;
    const WriteDocument in = make_document();
    const auto written = write_onf(in, dir.file("out.onf"));
    REQUIRE_MESSAGE(written.has_value(), written.error().message);
    CHECK(dir.file_count() == 1);  // no ".part" file is left

    CollectingSink sink;
    const Document out = open_file(dir.file("out.onf"), &sink);
    CHECK(sink.items().empty());
    CHECK(out.manifest().title == in.title);
    CHECK(out.manifest().generator == in.generator);
    CHECK(out.manifest().created_at == in.created_at);
    CHECK(out.manifest().modified_at == in.modified_at);
    REQUIRE(out.pages().size() == 2);
    CHECK(out.pages()[0].id == "p00001");
    CHECK(out.pages()[1].id == "p00002");

    const auto first = out.page("p00001", &sink);
    REQUIRE_MESSAGE(first.has_value(), first.error().message);
    const PageMeta& meta = first.value().meta;
    CHECK(meta.width == 21000);
    CHECK(meta.background.kind == BackgroundKind::Dotted);
    CHECK(meta.background.spacing == 500);
    CHECK(meta.background.top_margin == 1000);

    REQUIRE(first.value().ink.size() == 4);
    const Stroke& a = std::get<Stroke>(first.value().ink[0]);
    const Stroke& source_a = std::get<Stroke>(in.pages[0].ink[0]);
    CHECK(a.stroke_id == 1);
    CHECK(a.layer == 2);
    CHECK(a.created_at == 1781433600000ull);
    CHECK(a.tool == ToolType::Fountain);
    CHECK(a.samples.x == source_a.samples.x);
    CHECK(a.samples.y == source_a.samples.y);
    CHECK(a.samples.pressure == source_a.samples.pressure);
    CHECK(a.samples.timestamp == source_a.samples.timestamp);
    CHECK(a.samples.tilt_x == source_a.samples.tilt_x);
    CHECK(a.samples.tilt_y == source_a.samples.tilt_y);
    CHECK(a.samples.azimuth == source_a.samples.azimuth);

    const Stroke& b = std::get<Stroke>(first.value().ink[1]);
    CHECK_FALSE(b.created_at.has_value());
    CHECK(b.flags.no_timestamp());
    CHECK(b.flags.no_taper_start());
    CHECK(b.flags.no_taper_end());
    CHECK_FALSE(a.flags.no_taper_start());
    CHECK(b.samples.timestamp.empty());
    CHECK(b.samples.x == std::vector<Coord>{-500, 2147483647});
    CHECK(b.samples.y == std::vector<Coord>{-600, -2147483647 - 1});

    const Shape& ellipse = std::get<Shape>(first.value().ink[2]);
    CHECK(std::get<Ellipse>(ellipse.geometry).rotation == 4500);
    const Shape& line = std::get<Shape>(first.value().ink[3]);
    CHECK(line.created_at == 7);
    CHECK(line.tool == ToolType::Highlighter);
    CHECK(std::get<Line>(line.geometry).y2 == -1000);

    REQUIRE(meta.text_objects.size() == 1);
    const TextObject& text = meta.text_objects[0];
    CHECK(text.content == in.pages[0].meta.text_objects[0].content);  // quotes, backslash, newline, tab, 2 and 3 and 4 byte characters
    CHECK(text.created_at == 1781433600000ull);
    REQUIRE(text.spans.size() == 2);
    CHECK(text.spans[0].bold == true);
    CHECK_FALSE(text.spans[0].italic.has_value());
    CHECK(text.spans[1].color == Color{255, 0, 0, 128});
    CHECK(text.spans[1].strikethrough == false);
    CHECK(text.spans[1].font_family == "Mono");

    REQUIRE(meta.assets.size() == 2);
    CHECK(meta.assets[1].pdf_page == 7);
    CHECK(meta.assets[1].created_at == 1);
    CHECK_FALSE(meta.assets[0].pdf_page.has_value());
    CHECK(meta.assets[0].crop == Crop{1, 2, 30, 20});
    CHECK_FALSE(meta.assets[1].crop.has_value());

    // The embedded files are found, intact and under their hash.
    const auto image = out.asset(meta.assets[0], "p00001", &sink);
    REQUIRE(image.has_value());
    CHECK(image.value().check.status == AssetStatus::Ok);
    CHECK(image.value().bytes == in.assets[0].bytes);
    CHECK(out.asset(meta.assets[1], "p00001", &sink).value().check.status == AssetStatus::Ok);
    CHECK(sink.items().empty());

    const auto thumbnail = out.thumbnail_bytes();
    REQUIRE(thumbnail.has_value());
    CHECK(thumbnail.value() == in.thumbnail);

    const auto second = out.page("p00002");
    REQUIRE(second.has_value());
    CHECK(second.value().ink.empty());
    CHECK(second.value().meta.background.kind == BackgroundKind::Blank);
}

TEST_CASE("writer: the archive layout of spec 4.2 and the compression of spec 4.5") {
    TempDir dir;
    REQUIRE(write_onf(make_document(), dir.file("out.onf")).has_value());

    auto source = ByteSource::from_file(dir.file("out.onf"));
    REQUIRE(source.has_value());
    auto archive = ZipArchive::open(std::move(source).value());
    REQUIRE_MESSAGE(archive.has_value(), archive.error().message);

    bool assets_directory = false;
    for (const ZipEntryInfo& entry : archive.value().entries()) {
        CAPTURE(entry.name);
        if (entry.name == "assets/") {
            assets_directory = true;
            CHECK(entry.is_directory);
        } else if (entry.name.ends_with("strokes.bin") || entry.name.starts_with("assets/") || entry.name == "thumbnail.png") {
            CHECK(entry.compressed_size == entry.size);  // stored
        } else if (entry.name == "manifest.json" || entry.name.ends_with("meta.json")) {
            CHECK(entry.compressed_size < entry.size);  // deflated
        }
    }
    CHECK(assets_directory);
    for (const char* name : {"manifest.json", "pages/p00001/meta.json", "pages/p00001/strokes.bin", "pages/p00002/meta.json", "pages/p00002/strokes.bin",
                             "thumbnail.png"}) {
        CAPTURE(name);
        CHECK(archive.value().find(name).has_value());
    }
}

TEST_CASE("writer: the same document gives the same bytes") {
    TempDir dir;
    REQUIRE(write_onf(make_document(), dir.file("a.onf")).has_value());
    // ZIP times have a resolution of two seconds: nothing in the file may depend on the clock.
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    REQUIRE(write_onf(make_document(), dir.file("b.onf")).has_value());
    std::ifstream a(dir.file("a.onf"), std::ios::binary), b(dir.file("b.onf"), std::ios::binary);
    const std::string bytes_a((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
    const std::string bytes_b((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    CHECK(bytes_a == bytes_b);
    CHECK(bytes_a.size() > 500);
}

TEST_CASE("writer: strokes.bin matches the vectors of spec Appendix C byte for byte") {
    // V1 to V7 as model objects. The expected bytes are the chunks of ink_chunks.tsv, extracted from the specification.
    WriteDocument d;
    d.title = "Vectors";
    d.created_at = time_of(2026, 6, 14);
    d.modified_at = d.created_at;
    WritePage page;
    page.meta.width = 21000;
    page.meta.height = 29700;
    page.meta.background.type = "blank";
    page.meta.background.color = {255, 255, 255, 255};

    Stroke v1;
    v1.stroke_id = 1;
    v1.layer = 2;
    v1.color = {0, 0, 0, 255};
    v1.base_width = 100;
    v1.flags = StrokeFlags{0x000C};
    v1.samples.x = {10000, 10004};
    v1.samples.y = {5000, 5003};
    v1.samples.pressure = {255, 255};
    page.ink.push_back(v1);

    Stroke v2;
    v2.stroke_id = 2;
    v2.layer = 1;
    v2.created_at = 1781433600000ull;
    v2.color = {0, 0, 0, 255};
    v2.tool_type_raw = 1;
    v2.tool = ToolType::Fountain;
    v2.base_width = 100;
    v2.samples.x = {10000, 10004};
    v2.samples.y = {5000, 5003};
    v2.samples.pressure = {200, 202};
    v2.samples.timestamp = {0, 8};
    page.ink.push_back(v2);

    Stroke v7;
    v7.stroke_id = 7;
    v7.layer = 1;
    v7.color = {0, 0, 0, 255};
    v7.tool_type_raw = 1;
    v7.tool = ToolType::Fountain;
    v7.base_width = 100;
    v7.flags = StrokeFlags{0x006C};
    v7.samples.x = {1000, 1010};
    v7.samples.y = {2000, 2005};
    v7.samples.pressure = {255, 255};
    page.ink.push_back(v7);

    Shape v3 = make_ellipse(3, 2);
    page.ink.push_back(v3);

    Shape v4;
    v4.stroke_id = 4;
    v4.layer = 3;
    v4.created_at = 1781433600001ull;
    v4.color = {0, 0, 0, 0x66};
    v4.tool_type_raw = 3;
    v4.tool = ToolType::Highlighter;
    v4.base_width = 400;
    v4.geometry = Rectangle{10500, 14850, 6000, 3000, 9000};
    page.ink.push_back(v4);

    Shape v5;
    v5.stroke_id = 5;
    v5.layer = 1;
    v5.color = {0, 0, 0, 255};
    v5.base_width = 100;
    v5.geometry = Line{-500, 2000, 1000, -1000};
    page.ink.push_back(v5);

    Shape v6;
    v6.stroke_id = 6;
    v6.layer = 1;
    v6.color = {0, 0, 0, 255};
    v6.tool_type_raw = 2;
    v6.tool = ToolType::Pencil;
    v6.base_width = 150;
    v6.geometry = Triangle{5000, 5000, 8000, 5000, 6500, 7500};
    page.ink.push_back(v6);
    // V3 in the specification uses a red colour, which make_ellipse also has.

    d.pages = {page};
    TempDir dir;
    REQUIRE_MESSAGE(write_onf(d, dir.file("v.onf")).has_value(), "write failed");

    auto source = ByteSource::from_file(dir.file("v.onf"));
    auto archive = ZipArchive::open(std::move(source).value());
    REQUIRE(archive.has_value());
    const auto index = archive.value().find("pages/p00001/strokes.bin");
    REQUIRE(index.has_value());
    const auto written = archive.value().read(*index);
    REQUIRE(written.has_value());

    Bytes expected = file_header();
    for (const Row& row : load_vectors("ink_chunks.tsv")) append(expected, parse_hex_bytes(row.at(2)));
    append(expected, chunk(0x0002));
    CHECK(written.value() == expected);
}

TEST_CASE("writer: a document that breaks a rule is refused and nothing is written") {
    TempDir dir;
    struct Case {
        const char* name;
        void (*apply)(WriteDocument&);
        const char* mentions;
    };
    const std::vector<Case> cases = {
        {"no pages", [](WriteDocument& d) { d.pages.clear(); }, "1 to 99999 pages"},
        {"bad date", [](WriteDocument& d) { d.created_at.month = 13; }, "created_at"},
        {"leap day in a common year", [](WriteDocument& d) { d.modified_at = time_of(2027, 2, 29); }, "modified_at"},
        {"title too long", [](WriteDocument& d) { d.title = std::string(70000, 'x'); }, "title"},
        {"title not UTF-8", [](WriteDocument& d) { d.title = "\xC3"; }, "UTF-8"},
        {"page size", [](WriteDocument& d) { d.pages[0].meta.width = 0; }, "width and height"},
        {"background type", [](WriteDocument& d) { d.pages[0].meta.background.type = "lined"; }, "background type"},
        {"background spacing", [](WriteDocument& d) { d.pages[0].meta.background.spacing = 0; }, "spacing"},
        {"negative top_margin", [](WriteDocument& d) { d.pages[0].meta.background.top_margin = -1; }, "top_margin"},
        {"top_margin on a blank page", [](WriteDocument& d) {
             d.pages[0].meta.background.type = "blank";
             d.pages[0].meta.background.kind = BackgroundKind::Blank;
             d.pages[0].meta.background.foreground_color.reset();
             d.pages[0].meta.background.spacing.reset();
             d.pages[0].meta.background.top_margin = 5;
         }, "top_margin"},
        {"layer 0", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).layer = 0; }, "layer"},
        {"layer of a shape", [](WriteDocument& d) { std::get<Shape>(d.pages[0].ink[2]).layer = 0; }, "layer"},
        {"layer of text", [](WriteDocument& d) { d.pages[0].meta.text_objects[0].layer = 0; }, "layer"},
        {"layer of an asset", [](WriteDocument& d) { d.pages[0].meta.assets[0].layer = 0; }, "layer"},
        {"duplicate stroke_id", [](WriteDocument& d) { std::get<Shape>(d.pages[0].ink[2]).stroke_id = 1; }, "not unique"},
        {"stroke without samples", [](WriteDocument& d) {
             Stroke& s = std::get<Stroke>(d.pages[0].ink[1]);
             s.samples = {};
         }, "sample_count"},
        {"column lengths", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).samples.y.pop_back(); }, "different lengths"},
        {"timestamps with NO_TIMESTAMP", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[1]).samples.timestamp = {1, 2}; }, "timestamp"},
        {"timestamps missing", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).samples.timestamp.clear(); }, "timestamp"},
        {"timestamps decrease", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).samples.timestamp = {5, 4, 9}; }, "decrease"},
        {"tilt out of range", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).samples.tilt_x[0] = 91; }, "tilt"},
        {"azimuth out of range", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).samples.azimuth[0] = 36000; }, "azimuth"},
        {"NO_PRESSURE with pressure", [](WriteDocument& d) {
             Stroke& s = std::get<Stroke>(d.pages[0].ink[1]);
             s.flags = StrokeFlags{static_cast<std::uint16_t>(s.flags.raw() | 4)};
             s.samples.pressure[0] = 100;
         }, "NO_PRESSURE"},
        {"reserved flag bit", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).flags = StrokeFlags{static_cast<std::uint16_t>(0x80)}; }, "reserved flag"},
        {"removed tool type 4", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).tool_type_raw = 4; }, "tool type"},
        {"reserved tool type", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).tool_type_raw = 5; }, "tool type"},
        {"base_width 0", [](WriteDocument& d) { std::get<Stroke>(d.pages[0].ink[0]).base_width = 0; }, "base_width"},
        {"shape with the eraser", [](WriteDocument& d) { std::get<Shape>(d.pages[0].ink[2]).tool_type_raw = 4; }, "tool type 0 to 3"},
        {"ellipse radius", [](WriteDocument& d) { std::get<Ellipse>(std::get<Shape>(d.pages[0].ink[2]).geometry).rx = 0; }, "rx and ry"},
        {"rotation", [](WriteDocument& d) { std::get<Ellipse>(std::get<Shape>(d.pages[0].ink[2]).geometry).rotation = 36000; }, "rotation"},
        {"duplicate text id", [](WriteDocument& d) { d.pages[0].meta.text_objects.push_back(d.pages[0].meta.text_objects[0]); }, "not unique"},
        {"generic family", [](WriteDocument& d) { d.pages[0].meta.text_objects[0].generic_family = "cursive"; }, "generic_family"},
        {"font size", [](WriteDocument& d) { d.pages[0].meta.text_objects[0].font_size = 0; }, "font_size"},
        {"span overlap", [](WriteDocument& d) { d.pages[0].meta.text_objects[0].spans[1].start = 3; }, "overlap"},
        {"span beyond content", [](WriteDocument& d) { d.pages[0].meta.text_objects[0].spans[1].end = 500; }, "beyond the content"},
        {"span font without generic", [](WriteDocument& d) { d.pages[0].meta.text_objects[0].spans[1].generic_family.reset(); }, "generic_family"},
        {"created_at too large", [](WriteDocument& d) { d.pages[0].meta.text_objects[0].created_at = 9007199254740992ull; }, "created_at"},
        {"asset not embedded", [](WriteDocument& d) { d.assets.clear(); }, "not among the assets"},
        {"asset with another extension", [](WriteDocument& d) { d.pages[0].meta.assets[0].extension = "jpg"; }, "not among the assets"},
        {"pdf_page missing", [](WriteDocument& d) { d.pages[0].meta.assets[1].pdf_page.reset(); }, "pdf_page"},
        {"pdf_page for an image", [](WriteDocument& d) { d.pages[0].meta.assets[0].pdf_page = 1; }, "pdf_page"},
        {"unsupported asset format", [](WriteDocument& d) { d.assets.push_back({"gif", {'G', 'I', 'F', '8'}}); }, "format"},
        {"asset that is not what it says", [](WriteDocument& d) { d.assets.push_back({"png", {'n', 'o', 'p', 'e'}}); }, "PNG signature"},
        {"crop outside the image", [](WriteDocument& d) { d.pages[0].meta.assets[0].crop = Crop{11, 0, 30, 20}; }, "inside the image"},
        {"crop beyond the height", [](WriteDocument& d) { d.pages[0].meta.assets[0].crop = Crop{0, 11, 30, 20}; }, "inside the image"},
        {"crop sum overflows", [](WriteDocument& d) { d.pages[0].meta.assets[0].crop = Crop{2147483647, 0, 2147483647, 1}; }, "inside the image"},
        {"crop negative x", [](WriteDocument& d) { d.pages[0].meta.assets[0].crop = Crop{-1, 0, 5, 5}; }, "at least 0"},
        {"crop width zero", [](WriteDocument& d) { d.pages[0].meta.assets[0].crop = Crop{0, 0, 0, 5}; }, "at least 1"},
        {"crop on a pdf", [](WriteDocument& d) { d.pages[0].meta.assets[1].crop = Crop{0, 0, 1, 1}; }, "must not be written for a pdf"},
        {"duplicate asset id", [](WriteDocument& d) { d.pages[0].meta.assets[1].id = "a1"; }, "not unique"},
        {"thumbnail too large", [](WriteDocument& d) { d.thumbnail = make_png(300, 10); }, "256 pixels"},
        {"thumbnail not a PNG", [](WriteDocument& d) { d.thumbnail = Bytes{1, 2, 3}; }, "PNG"},
    };

    for (const Case& c : cases) {
        CAPTURE(c.name);
        WriteDocument d = make_document();
        REQUIRE(!check_writable(d).has_value());
        c.apply(d);

        const std::string message = problem_of(d);
        CAPTURE(message);
        CHECK(message.find(c.mentions) != std::string::npos);
        CHECK(message.find("(spec ") != std::string::npos);

        const auto result = write_onf(d, dir.file("refused.onf"));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::WriteInvalidDocument);
        CHECK(dir.file_count() == 0);
    }
}

TEST_CASE("writer: an error while writing leaves nothing behind") {
    TempDir dir;
    const auto result = write_onf(make_document(), dir.file("missing_directory") / "out.onf");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::WriteFailed);
    CHECK(dir.file_count() == 0);
}

TEST_CASE("writer: the same asset is stored once, and an existing file is replaced") {
    TempDir dir;
    WriteDocument d = make_document();
    d.assets.push_back(d.assets[0]);
    d.assets.push_back(d.assets[0]);

    {
        std::ofstream old(dir.file("out.onf"));
        old << "an old file";
    }
    REQUIRE(write_onf(d, dir.file("out.onf")).has_value());

    auto source = ByteSource::from_file(dir.file("out.onf"));
    auto archive = ZipArchive::open(std::move(source).value());
    REQUIRE(archive.has_value());
    std::size_t png_files = 0;
    for (const ZipEntryInfo& entry : archive.value().entries()) {
        if (entry.name.starts_with("assets/") && entry.name.ends_with(".png")) ++png_files;
    }
    CHECK(png_files == 1);
}

TEST_CASE("writer: written pages keep their order and ids for many pages") {
    TempDir dir;
    WriteDocument d = make_document();
    d.thumbnail.reset();
    const WritePage plain = d.pages[1];
    d.pages.assign(120, plain);

    REQUIRE(write_onf(d, dir.file("many.onf")).has_value());
    const Document out = open_file(dir.file("many.onf"));
    REQUIRE(out.pages().size() == 120);
    CHECK(out.pages()[0].id == "p00001");
    CHECK(out.pages()[119].id == "p00120");
    CHECK(out.pages()[119].order == 120);
}
