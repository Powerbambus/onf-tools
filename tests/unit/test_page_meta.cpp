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

// A complete text object (spec 8.3). `content_field` replaces the content member, e.g. R"("content":"Hi")".
std::string text_object_json(const std::string& content_field = R"("content":"Hello")") {
    return R"({"id":"t1","layer":2,"x":10,"y":-20,"width":8000,"height":4000,)" + content_field +
           R"(,"font_family":"Helvetica","generic_family":"sans-serif","font_size":423,"color":"#000000FF",)"
           R"("bold":false,"italic":true})";
}

std::string asset_json(const std::string& extension = "pdf", const std::string& extra = R"(,"pdf_page":3)") {
    return R"({"id":"a1","layer":1,"hash":")" + std::string(64, 'a') + R"(","extension":")" + extension +
           R"(","x":0,"y":0,"width":21000,"height":29700)" + extra + "}";
}

std::string meta_with(const std::string& text_objects, const std::string& assets) {
    return meta(dotted(), "21000", "29700", R"("text_objects":[)" + text_objects + R"(],"assets":[)" + assets + "]");
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
    const std::string text_object = text_object_json(R"("content":")" + long_text + R"(")");
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

TEST_CASE("meta: a complete text object and a PDF placement") {
    const PageMeta m = accepted(meta_with(text_object_json(), asset_json()));

    REQUIRE(m.text_objects.size() == 1);
    const TextObject& t = m.text_objects[0];
    CHECK(t.id == "t1");
    CHECK(t.layer == 2);
    CHECK_FALSE(t.created_at.has_value());
    CHECK(t.x == 10);
    CHECK(t.y == -20);
    CHECK(t.width == 8000);
    CHECK(t.content == "Hello");
    CHECK(t.generic_family == "sans-serif");
    CHECK(t.font_size == 423);
    CHECK(t.color == Color{0, 0, 0, 255});
    CHECK_FALSE(t.bold);
    CHECK(t.italic);
    CHECK(t.spans.empty());

    REQUIRE(m.assets.size() == 1);
    const AssetRef& a = m.assets[0];
    CHECK(a.id == "a1");
    CHECK(a.layer == 1);
    CHECK(a.extension == "pdf");
    CHECK(a.pdf_page == 3);
    CHECK(a.width == 21000);
}

TEST_CASE("meta: created_at of an object is optional") {
    const std::string with_created =
        R"({"id":"t1","created_at":1781433600000,"layer":1,"x":0,"y":0,"width":1,"height":1,"content":"","font_family":"F",)"
        R"("generic_family":"serif","font_size":1,"color":"#00000000","bold":false,"italic":false})";
    CHECK(accepted(meta_with(with_created, "")).text_objects[0].created_at == 1781433600000ull);
    CHECK_FALSE(accepted(meta_with(text_object_json(), "")).text_objects[0].created_at.has_value());
    CHECK(problem(meta_with(replace_first(with_created, "1781433600000", "\"x\""), "")) == ErrorCode::MetaInvalidField);
    CHECK(problem(meta_with(replace_first(with_created, "1781433600000", "-1"), "")) == ErrorCode::MetaInvalidField);
}

TEST_CASE("meta: the layer of an object is 1 to 65535") {
    const std::string text = text_object_json();
    for (const char* good : {"1", "2", "65535"}) {
        CAPTURE(good);
        CHECK(accepted(meta_with(replace_first(text, "\"layer\":2", std::string("\"layer\":") + good), "")).text_objects.size() == 1);
    }
    for (const char* bad : {"0", "65536", "-1", "4294967296"}) {
        CAPTURE(bad);
        CHECK(problem(meta_with(replace_first(text, "\"layer\":2", std::string("\"layer\":") + bad), "")) == ErrorCode::LayerInvalid);
        CHECK(problem(meta_with("", replace_first(asset_json(), "\"layer\":1", std::string("\"layer\":") + bad))) == ErrorCode::LayerInvalid);
    }
    for (const char* bad : {"\"middle\"", "null", "true", "[]"}) {
        CAPTURE(bad);
        const ErrorCode code = problem(meta_with(replace_first(text, "\"layer\":2", std::string("\"layer\":") + bad), ""));
        CHECK(code == ErrorCode::MetaInvalidField);
    }
    CHECK(problem(meta_with(replace_first(text, "\"layer\":2,", ""), "")) == ErrorCode::MetaInvalidField);
    CHECK(problem(meta_with("", replace_first(asset_json(), "\"layer\":1,", ""))) == ErrorCode::MetaInvalidField);
}

TEST_CASE("meta: text object fields") {
    const std::string text = text_object_json();
    struct Case { const char* from; const char* to; };
    for (const Case& c : {Case{"\"width\":8000", "\"width\":0"}, Case{"\"height\":4000", "\"height\":-5"},
                          Case{"\"font_size\":423", "\"font_size\":0"}, Case{"\"color\":\"#000000FF\"", "\"color\":\"black\""},
                          Case{"\"bold\":false", "\"bold\":0"}, Case{"\"font_family\":\"Helvetica\",", ""},
                          Case{"\"id\":\"t1\",", ""}, Case{"\"x\":10,", ""},
                          Case{"\"generic_family\":\"sans-serif\",", ""}, Case{"\"x\":10", "\"x\":2147483648"}}) {
        CAPTURE(c.from);
        CHECK(problem(meta_with(replace_first(text, c.from, c.to), "")) == ErrorCode::MetaInvalidField);
    }
    CHECK(problem(meta_with("1", "")) == ErrorCode::MetaInvalidField);

    // An unknown generic_family and unknown keys are not errors (spec 8.4.1, 10.5.1).
    CHECK(accepted(meta_with(replace_first(text, "sans-serif", "cursive"), "")).text_objects[0].generic_family == "cursive");
    CHECK(accepted(meta_with(replace_first(text, "\"id\"", "\"extra\":1,\"id\""), "")).text_objects.size() == 1);
}

TEST_CASE("meta: ids of text objects and of assets are unique within a page") {
    const std::string text = text_object_json();
    CHECK(problem(meta_with(text + "," + text, "")) == ErrorCode::DuplicateObjectId);
    CHECK(problem(meta_with("", asset_json() + "," + asset_json())) == ErrorCode::DuplicateObjectId);
    // A text object and an asset may share an id: the ids are per array.
    CHECK(accepted(meta_with(replace_first(text, "\"t1\"", "\"x1\""), replace_first(asset_json(), "\"a1\"", "\"x1\""))).assets.size() == 1);
}

TEST_CASE("meta: spans") {
    const auto with_spans = [](const std::string& spans) {
        return meta_with(text_object_json(R"("content":"abcdef")") .replace(text_object_json(R"("content":"abcdef")").size() - 1, 1, R"(,"spans":)" + spans + "}"), "");
    };

    const PageMeta m = accepted(with_spans(R"([{"start":0,"end":2,"bold":true,"italic":false},)"
                                           R"({"start":3,"end":6,"underline":true,"strikethrough":true,"color":"#FF0000FF",)"
                                           R"("font_size":600,"font_family":"Mono","generic_family":"monospace","unknown":1}])"));
    const std::vector<Span>& spans = m.text_objects[0].spans;
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start == 0);
    CHECK(spans[0].end == 2);
    CHECK(spans[0].bold == true);
    CHECK(spans[0].italic == false);
    CHECK_FALSE(spans[0].underline.has_value());
    CHECK_FALSE(spans[0].color.has_value());
    CHECK(spans[1].underline == true);
    CHECK(spans[1].strikethrough == true);
    CHECK(spans[1].color == Color{255, 0, 0, 255});
    CHECK(spans[1].font_size == 600);
    CHECK(spans[1].font_family == "Mono");
    CHECK(spans[1].generic_family == "monospace");

    // Gaps are valid, the end may be the end of the content, an empty array is valid.
    CHECK(accepted(with_spans(R"([{"start":1,"end":2},{"start":2,"end":6}])")).text_objects[0].spans.size() == 2);
    CHECK(accepted(with_spans("[]")).text_objects[0].spans.empty());

    for (const char* bad : {
             R"([{"start":0,"end":7}])",                          // beyond the content
             R"([{"start":2,"end":2}])",                          // empty
             R"([{"start":3,"end":2}])",                          // reversed
             R"([{"start":0,"end":3},{"start":2,"end":4}])",      // overlap
             R"([{"start":2,"end":4},{"start":0,"end":1}])",      // not sorted
             R"([{"start":-1,"end":2}])",                         // negative
             R"([{"start":0}])",                                  // end missing
             R"([{"end":2}])",                                    // start missing
             R"([{"start":0,"end":1,"bold":"yes"}])",             // wrong type
             R"([{"start":0,"end":1,"color":"red"}])",            // not a color
             R"([{"start":0,"end":1,"font_size":0}])",            // not positive
             R"([{"start":0,"end":1,"font_family":"F"}])",        // generic_family missing
             R"([1])", R"({})", "null"}) {
        CAPTURE(bad);
        CHECK(problem(with_spans(bad)) == ErrorCode::SpansInvalid);
    }
}

TEST_CASE("meta: spans count code points, not bytes") {
    // "aé€😀" is 4 code points and 10 bytes in UTF-8.
    const std::string content = R"("content":"a\u00e9\u20ac\ud83d\ude00")";
    std::string text = text_object_json(content);
    text.insert(text.size() - 1, R"(,"spans":[{"start":3,"end":4}])");
    CHECK(accepted(meta_with(text, "")).text_objects[0].spans.size() == 1);

    std::string beyond = text_object_json(content);
    beyond.insert(beyond.size() - 1, R"(,"spans":[{"start":3,"end":5}])");
    CHECK(problem(meta_with(beyond, "")) == ErrorCode::SpansInvalid);
}

TEST_CASE("meta: the number of spans is limited to 65535") {
    const auto spans_of = [](int count) {
        std::string spans = "[";
        for (int i = 0; i < count; ++i) {
            if (i > 0) spans += ",";
            spans += R"({"start":)" + std::to_string(i) + R"(,"end":)" + std::to_string(i + 1) + "}";
        }
        std::string text = text_object_json(R"("content":")" + std::string(70000, 'x') + R"(")");
        text.insert(text.size() - 1, R"(,"spans":)" + spans + "]");
        return meta_with(text, "");
    };
    // The content needs more than 65535 code points, so the long limit of text_objects[].content applies.
    const auto parse_long = [](const std::string& text) {
        auto json = parse_json(text, JsonOptions{true});
        REQUIRE_MESSAGE(json.has_value(), json.error().message);
        return parse_page_meta(json.value(), "p00004");
    };
    const auto ok = parse_long(spans_of(65535));
    REQUIRE_MESSAGE(ok.has_value(), ok.error().message);
    CHECK(ok.value().text_objects[0].spans.size() == 65535);
    const auto too_many = parse_long(spans_of(65536));
    REQUIRE_FALSE(too_many.has_value());
    CHECK(too_many.error().code == ErrorCode::SpansInvalid);
}

TEST_CASE("meta: asset placements") {
    // The hash is 64 lowercase hexadecimal digits (spec 4.6.1).
    for (const std::string& bad : {std::string(63, 'a'), std::string(65, 'a'), std::string(64, 'A'), std::string(63, 'a') + "g",
                                   std::string("../") + std::string(61, 'a')}) {
        CAPTURE(bad);
        CHECK(problem(meta_with("", replace_first(asset_json(), std::string(64, 'a'), bad))) == ErrorCode::InvalidIdentifier);
    }
    CHECK(accepted(meta_with("", replace_first(asset_json(), std::string(64, 'a'), "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"))).assets.size() == 1);

    // Unknown extensions are no error (spec 9.2.2).
    const PageMeta unknown = accepted(meta_with("", asset_json("gif", "")));
    CHECK(unknown.assets[0].extension == "gif");
    CHECK_FALSE(unknown.assets[0].pdf_page.has_value());

    // pdf_page is required for PDF, 1 to 2147483647, and ignored for other extensions (spec 9.3.2).
    CHECK(problem(meta_with("", asset_json("pdf", ""))) == ErrorCode::MetaInvalidField);
    for (const char* bad : {"0", "-1", "2147483648", "\"3\"", "null"}) {
        CAPTURE(bad);
        const ErrorCode code = problem(meta_with("", asset_json("pdf", std::string(R"(,"pdf_page":)") + bad)));
        CHECK(code == ErrorCode::MetaInvalidField);
    }
    CHECK(accepted(meta_with("", asset_json("pdf", R"(,"pdf_page":2147483647)"))).assets[0].pdf_page == 2147483647);
    CHECK(accepted(meta_with("", asset_json("png", R"(,"pdf_page":0)"))).assets[0].pdf_page == std::nullopt);

    for (const char* bad : {"\"width\":0", "\"height\":-1"}) {
        CAPTURE(bad);
        const std::string key = std::string(bad).substr(0, std::string(bad).find(':'));
        CHECK(problem(meta_with("", replace_first(asset_json(), key + (key == "\"width\"" ? ":21000" : ":29700"), bad))) == ErrorCode::MetaInvalidField);
    }
    CHECK(problem(meta_with("", "1")) == ErrorCode::MetaInvalidField);
}
