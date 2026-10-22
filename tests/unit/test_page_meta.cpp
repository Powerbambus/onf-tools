#include "doctest.h"

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "onf/errors.h"
#include "onf/read/json.h"
#include "onf/read/page_meta.h"
#include "support/zip_builder.h"

using namespace onf;
using namespace onf::test;

namespace {

const std::string kSpecExample = R"({
    "width": 21000,
    "height": 29700,
    "background": {
        "type": "dotted",
        "color": "#FFFFFFFF",
        "foreground_color": "#000000FF",
        "spacing": 500
    },
    "text_objects": [],
    "assets": []
})";

std::string meta(const std::string& background, const std::string& width = "21000", const std::string& height = "29700",
                 const std::string& arrays = R"("text_objects":[],"assets":[])") {
    return R"({"width":)" + width + R"(,"height":)" + height + R"(,"background":)" + background + (arrays.empty() ? "" : "," + arrays) + "}";
}

std::string dotted(const std::string& spacing = "500", const std::string& foreground = "#000000FF") {
    return R"({"type":"dotted","color":"#FFFFFFFF","foreground_color":")" + foreground + R"(","spacing":)" + spacing + "}";
}

Result<PageMeta, PageError> parse_text(const std::string& text) {
    auto json = parse_json(text);
    REQUIRE_MESSAGE(json.has_value(), "test input is not valid JSON: " << json.error().message);
    return parse_page_meta(json.value(), "p00004");
}

PageMeta accepted(const std::string& text) {
    auto result = parse_text(text);
    REQUIRE_MESSAGE(result.has_value(), "unexpected error: " << result.error().message);
    return std::move(result).value();
}

ErrorCode problem(const std::string& text) {
    const auto result = parse_text(text);
    REQUIRE_MESSAGE(!result.has_value(), "unexpectedly accepted: " << text.substr(0, 200));
    CHECK(result.error().page_id == "p00004");
    return result.error().code;
}

std::string replace_first(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE_MESSAGE(at != std::string::npos, "test input does not contain " << from);
    text.replace(at, from.size(), to);
    return text;
}

Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

ZipEntrySpec entry(std::string name, const std::string& content = "") {
    ZipEntrySpec e;
    e.name = std::move(name);
    e.data = bytes_of(content);
    return e;
}

Result<PageMeta, PageError> read_from(const ZipSpec& spec, const char* page_id = "p00001") {
    auto archive = ZipArchive::open(ByteSource::from_memory(build_zip(spec)));
    REQUIRE_MESSAGE(archive.has_value(), "test archive was rejected: " << archive.error().message);
    return read_page_meta(archive.value(), page_id);
}

ZipSpec archive_with_meta(const std::string& content) {
    ZipSpec spec;
    spec.entries.push_back(entry("pages/p00001/meta.json", content));
    return spec;
}

}  // namespace

TEST_CASE("meta: the example from spec 6.2.1") {
    const PageMeta m = accepted(kSpecExample);
    CHECK(m.width == 21000);
    CHECK(m.height == 29700);
    CHECK(m.background.type == "dotted");
    CHECK(m.background.kind == BackgroundKind::Dotted);
    CHECK(m.background.color == Color{255, 255, 255, 255});
    CHECK(m.background.foreground_color == Color{0, 0, 0, 255});
    CHECK(m.background.spacing == 500);
}

TEST_CASE("meta: the four background types") {
    const std::string common = R"("color":"#FFFFFFFF","foreground_color":"#102030FF","spacing":250)";
    for (const char* type : {"ruled", "grid", "dotted"}) {
        CAPTURE(type);
        const PageMeta m = accepted(meta(std::string(R"({"type":")") + type + R"(",)" + common + "}"));
        CHECK(m.background.type == type);
        CHECK(m.background.foreground_color == Color{0x10, 0x20, 0x30, 0xFF});
        CHECK(m.background.spacing == 250);
    }
    CHECK(accepted(meta(R"({"type":"ruled",)" + common + "}")).background.kind == BackgroundKind::Ruled);
    CHECK(accepted(meta(R"({"type":"grid",)" + common + "}")).background.kind == BackgroundKind::Grid);

    const PageMeta blank = accepted(meta(R"({"type":"blank","color":"#FFFFFF80"})"));
    CHECK(blank.background.kind == BackgroundKind::Blank);
    CHECK(blank.background.color.alpha == 0x80);
    CHECK_FALSE(blank.background.foreground_color.has_value());
    CHECK_FALSE(blank.background.spacing.has_value());
}

TEST_CASE("meta: fields that a blank page does not need are ignored, even when odd") {
    const PageMeta m = accepted(meta(R"({"type":"blank","color":"#FFFFFFFF","foreground_color":"not a color","spacing":-4})"));
    CHECK(m.background.kind == BackgroundKind::Blank);
    CHECK_FALSE(m.background.spacing.has_value());
}

TEST_CASE("meta: an unknown background type only needs a color") {
    for (const char* type : {"lined", "Dotted", "BLANK", "", "isometric"}) {
        CAPTURE(type);
        const PageMeta m = accepted(meta(std::string(R"({"type":")") + type + R"(","color":"#ABCDEF01"})"));
        CHECK(m.background.kind == BackgroundKind::Unknown);
        CHECK(m.background.type == type);
        CHECK(m.background.color == Color{0xAB, 0xCD, 0xEF, 0x01});
    }
    CHECK(problem(meta(R"({"type":"lined"})")) == ErrorCode::BackgroundInvalidField);
    CHECK(problem(meta(R"({"type":"lined","color":"red"})")) == ErrorCode::BackgroundInvalidField);
}

TEST_CASE("meta: page size must be a positive 32-bit integer") {
    const std::string bg = dotted();
    CHECK(accepted(meta(bg, "1", "1")).width == 1);
    CHECK(accepted(meta(bg, "2147483647", "2147483647")).height == 2147483647);

    for (const char* bad : {"0", "-1", "2147483648", "9007199254740991", "-2147483648", "\"21000\"", "null", "true", "[]", "{}"}) {
        CAPTURE(bad);
        CHECK(problem(meta(bg, bad, "29700")) == ErrorCode::MetaInvalidField);
        CHECK(problem(meta(bg, "21000", bad)) == ErrorCode::MetaInvalidField);
    }
}

TEST_CASE("meta: page size is required") {
    CHECK(problem(replace_first(kSpecExample, "\"width\": 21000,", "")) == ErrorCode::MetaInvalidField);
    CHECK(problem(replace_first(kSpecExample, "\"height\": 29700,", "")) == ErrorCode::MetaInvalidField);
}

TEST_CASE("meta: spacing must be a positive 32-bit integer for ruled, grid and dotted pages") {
    CHECK(accepted(meta(dotted("1"))).background.spacing == 1);
    CHECK(accepted(meta(dotted("2147483647"))).background.spacing == 2147483647);
    for (const char* bad : {"0", "-500", "2147483648", "\"500\"", "null", "false"}) {
        CAPTURE(bad);
        CHECK(problem(meta(dotted(bad))) == ErrorCode::BackgroundInvalidField);
    }
    for (const char* type : {"ruled", "grid"}) {
        CAPTURE(type);
        CHECK(problem(meta(std::string(R"({"type":")") + type + R"(","color":"#FFFFFFFF","foreground_color":"#000000FF"})")) ==
              ErrorCode::BackgroundInvalidField);
    }
}

TEST_CASE("meta: colors") {
    CHECK(accepted(meta(dotted("500", "#abcdef12"))).background.foreground_color == Color{0xAB, 0xCD, 0xEF, 0x12});
    for (const char* bad : {"#FFF", "#FFFFFF", "FFFFFFFF", "#FFFFFFFFF", "#GGGGGGGG", " #FFFFFFFF", "rgb(0,0,0)", ""}) {
        CAPTURE(bad);
        CHECK(problem(meta(dotted("500", bad))) == ErrorCode::BackgroundInvalidField);
        CHECK(problem(meta(replace_first(dotted(), "\"#FFFFFFFF\"", std::string("\"") + bad + "\""))) == ErrorCode::BackgroundInvalidField);
    }
    CHECK(problem(meta(replace_first(dotted(), "\"#000000FF\"", "255"))) == ErrorCode::BackgroundInvalidField);
    CHECK(problem(meta(replace_first(dotted(), "\"foreground_color\":\"#000000FF\",", ""))) == ErrorCode::BackgroundInvalidField);
    CHECK(problem(meta(replace_first(dotted(), "\"color\":\"#FFFFFFFF\",", ""))) == ErrorCode::BackgroundInvalidField);
}

TEST_CASE("meta: the background object") {
    CHECK(problem(replace_first(kSpecExample, "\"background\": {", "\"unknown\": {")) == ErrorCode::MetaInvalidField);
    for (const char* bad : {"null", "[]", "\"dotted\"", "5"}) {
        CAPTURE(bad);
        CHECK(problem(meta(bad)) == ErrorCode::BackgroundInvalidField);
    }
    CHECK(problem(meta(R"({"color":"#FFFFFFFF"})")) == ErrorCode::BackgroundInvalidField);
    CHECK(problem(meta(R"({"type":5,"color":"#FFFFFFFF"})")) == ErrorCode::BackgroundInvalidField);
    CHECK(problem(meta(R"({"type":null,"color":"#FFFFFFFF"})")) == ErrorCode::BackgroundInvalidField);
}

TEST_CASE("meta: text_objects and assets must be arrays") {
    const std::string bg = dotted();
    CHECK(accepted(meta(bg, "21000", "29700", R"("text_objects":[],"assets":[])")).width == 21000);
    CHECK(accepted(meta(bg, "21000", "29700", R"("text_objects":[1,"x",null,{"any":"thing"}],"assets":[[]])")).width == 21000);

    for (const char* bad : {R"("text_objects":{},"assets":[])", R"("text_objects":[],"assets":null)",
                            R"("text_objects":"none","assets":[])", R"("assets":[])", R"("text_objects":[])", ""}) {
        CAPTURE(bad);
        CHECK(problem(meta(bg, "21000", "29700", bad)) == ErrorCode::MetaInvalidField);
    }
}

TEST_CASE("meta: unknown keys are ignored") {
    const PageMeta m = accepted(replace_first(kSpecExample, "\"width\": 21000,", "\"width\": 21000, \"future\": {\"x\": [1, 2, 3]},"));
    CHECK(m.width == 21000);
}

TEST_CASE("meta: the root must be an object") {
    for (const char* bad : {"[]", "null", "5", "\"meta\"", "true"}) {
        CAPTURE(bad);
        CHECK(problem(bad) == ErrorCode::MetaInvalidField);
    }
}

TEST_CASE("meta: read from an archive") {
    const PageMeta m = read_from(archive_with_meta(kSpecExample)).value();
    CHECK(m.width == 21000);
    CHECK(m.background.kind == BackgroundKind::Dotted);
}

TEST_CASE("meta: a missing meta.json is a page error") {
    ZipSpec spec;
    spec.entries.push_back(entry("pages/p00001/strokes.bin"));
    const auto result = read_from(spec);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::PageFileMissing);
    CHECK(result.error().page_id == "p00001");

    CHECK(read_from(archive_with_meta(kSpecExample), "p00002").error().code == ErrorCode::PageFileMissing);
    CHECK(read_from(ZipSpec{}).error().code == ErrorCode::PageFileMissing);
}

TEST_CASE("meta: JSON errors are page errors with the JSON code and a position") {
    struct Case {
        std::string json;
        ErrorCode code;
    };
    const std::vector<Case> cases = {
        {"{", ErrorCode::JsonSyntax},
        {"", ErrorCode::JsonSyntax},
        {"\xEF\xBB\xBF{}", ErrorCode::JsonBom},
        {R"({"width":1,"width":2})", ErrorCode::JsonDuplicateKey},
        {R"({"width":1.5})", ErrorCode::JsonNonIntegerNumber},
        {R"({"width":9007199254740992})", ErrorCode::JsonIntegerOutOfRange},
    };
    for (const Case& c : cases) {
        CAPTURE(c.json);
        const auto result = read_from(archive_with_meta(c.json));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == c.code);
        CHECK(result.error().page_id == "p00001");
        CHECK(result.error().offset.has_value());
    }
}

TEST_CASE("meta: long text content is allowed only where the spec allows it") {
    const std::string long_text(100000, 'x');
    const std::string text_object = R"({"id":"t1","content":")" + long_text + R"("})";
    const std::string background = dotted();

    const std::string with_long_content =
        meta(background, "21000", "29700", R"("text_objects":[)" + text_object + R"(],"assets":[])");
    CHECK(read_from(archive_with_meta(with_long_content)).has_value());

    const std::string in_assets =
        meta(background, "21000", "29700", R"("text_objects":[],"assets":[)" + text_object + R"(])");
    CHECK(read_from(archive_with_meta(in_assets)).error().code == ErrorCode::JsonStringTooLong);

    const std::string wrong_key = meta(background, "21000", "29700",
                                       R"("text_objects":[{"id":")" + long_text + R"("}],"assets":[])");
    CHECK(read_from(archive_with_meta(wrong_key)).error().code == ErrorCode::JsonStringTooLong);
}

TEST_CASE("meta: mutated meta.json never crashes") {
    std::mt19937_64 rng(555);
    for (int round = 0; round < 3000; ++round) {
        std::string mutated = kSpecExample;
        const int flips = 1 + static_cast<int>(rng() % 3);
        for (int f = 0; f < flips; ++f) mutated[rng() % mutated.size()] = static_cast<char>(rng());
        auto json = parse_json(mutated);
        if (json.has_value()) static_cast<void>(parse_page_meta(json.value(), "p00001"));
    }
}
