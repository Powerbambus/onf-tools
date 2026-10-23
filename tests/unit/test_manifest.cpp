#include "doctest.h"

#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

#include "onf/errors.h"
#include "onf/limits.h"
#include "onf/read/json.h"
#include "onf/read/manifest.h"
#include "onf/read/values.h"
#include "support/zip_builder.h"

using namespace onf;
using namespace onf::test;

namespace {

const std::string kSpecExample = R"({
    "onf_version": "0.4.0",
    "title": "Lecture Notes",
    "created_at": "2026-06-14T10:32:00.000Z",
    "modified_at": "2026-06-14T12:15:00.000Z",
    "pages": [
        { "id": "p00001", "order": 1 },
        { "id": "p00002", "order": 2 },
        { "id": "p00003", "order": 3 }
    ]
})";

std::string page(const std::string& id, long order) {
    return "{\"id\":\"" + id + "\",\"order\":" + std::to_string(order) + "}";
}

std::string manifest_with_pages(const std::string& pages) {
    return R"({"onf_version":"0.4.0","title":"T","created_at":"2026-06-14T10:32:00.000Z",)"
           R"("modified_at":"2026-06-14T12:15:00.000Z","pages":[)" +
           pages + "]}";
}

std::string replace_first(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE_MESSAGE(at != std::string::npos, "test input does not contain " << from);
    text.replace(at, from.size(), to);
    return text;
}

Result<Manifest, FileError> manifest_of(const std::string& text) {
    auto json = parse_json(text);
    REQUIRE_MESSAGE(json.has_value(), "test input is not valid JSON: " << json.error().message);
    return parse_manifest(json.value());
}

ErrorCode problem(const std::string& text) {
    const auto result = manifest_of(text);
    REQUIRE_MESSAGE(!result.has_value(), "unexpectedly accepted: " << text.substr(0, 200));
    return result.error().code;
}

Bytes text_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

ZipEntrySpec entry(std::string name, const std::string& content = "") {
    ZipEntrySpec e;
    e.name = std::move(name);
    e.data = text_bytes(content);
    return e;
}

Result<ZipArchive, FileError> archive_of(const ZipSpec& spec) {
    return ZipArchive::open(ByteSource::from_memory(build_zip(spec)));
}

ZipSpec archive_with_manifest(const std::string& manifest, std::initializer_list<std::string> other_entries) {
    ZipSpec spec;
    spec.entries.push_back(entry("manifest.json", manifest));
    for (const std::string& name : other_entries) spec.entries.push_back(entry(name));
    return spec;
}

Result<Manifest, FileError> read_from(const ZipSpec& spec) {
    auto archive = archive_of(spec);
    REQUIRE_MESSAGE(archive.has_value(), "test archive was rejected: " << archive.error().message);
    return read_manifest(archive.value());
}

}  // namespace

TEST_CASE("manifest: the example from spec 6.1.1") {
    const auto result = manifest_of(kSpecExample);
    REQUIRE(result.has_value());
    const Manifest& m = result.value();
    CHECK(m.onf_version == Version{0, 4, 0});
    CHECK(m.title == "Lecture Notes");
    CHECK(m.created_at == UtcDateTime{2026, 6, 14, 10, 32, 0, 0});
    CHECK(m.modified_at == UtcDateTime{2026, 6, 14, 12, 15, 0, 0});
    REQUIRE(m.pages.size() == 3);
    CHECK(m.pages[0].id == "p00001");
    CHECK(m.pages[2].id == "p00003");
    CHECK(m.pages[2].order == 3);
}

TEST_CASE("manifest: pages are sorted by order whatever their position in the array") {
    const auto result =
        manifest_of(manifest_with_pages(page("p00001", 3) + "," + page("p00003", 1) + "," + page("p00002", 2)));
    REQUIRE(result.has_value());
    const auto& pages = result.value().pages;
    REQUIRE(pages.size() == 3);
    CHECK(pages[0].id == "p00003");
    CHECK(pages[1].id == "p00002");
    CHECK(pages[2].id == "p00001");
    for (std::uint32_t i = 0; i < 3; ++i) CHECK(pages[i].order == i + 1);
}

TEST_CASE("manifest: a single page, an empty title and unknown keys") {
    const std::string text =
        R"({"onf_version":"0.4.0","title":"","created_at":"2026-06-14T10:32:00.000Z","modified_at":"2026-06-14T10:32:00.000Z",)"
        R"("future":{"a":[1,2]},"pages":[{"id":"p00001","order":1,"extra":true}]})";
    const auto result = manifest_of(text);
    REQUIRE(result.has_value());
    CHECK(result.value().title.empty());
    CHECK(result.value().pages.size() == 1);
}

TEST_CASE("manifest: a version is parsed but not judged here") {
    const std::string text = manifest_with_pages(page("p00001", 1));
    for (const char* version : {"1.0.0", "0.1.4", "9.9.9"}) {
        CAPTURE(version);
        const auto result = manifest_of(replace_first(text, "0.4.0", version));
        REQUIRE(result.has_value());
        CHECK(result.value().onf_version == *parse_version(version));
    }
}

TEST_CASE("manifest: the version can be read on its own") {
    auto json = parse_json(R"({"onf_version":"2.5.7","pages":"not even valid"})");
    REQUIRE(json.has_value());
    const auto version = parse_manifest_version(json.value());
    REQUIRE(version.has_value());
    CHECK(version.value() == Version{2, 5, 7});

    for (const char* text : {"[]", "{}", R"({"onf_version":5})", R"({"onf_version":"1.0"})", R"({"onf_version":null})"}) {
        CAPTURE(text);
        const auto parsed = parse_json(text);
        REQUIRE(parsed.has_value());
        CHECK(parse_manifest_version(parsed.value()).error().code == ErrorCode::ManifestInvalidField);
    }
}

TEST_CASE("manifest: the root must be an object") {
    for (const char* text : {"[]", "\"manifest\"", "1", "null", "true"}) {
        CAPTURE(text);
        CHECK(problem(text) == ErrorCode::ManifestInvalidField);
    }
}

TEST_CASE("manifest: every required field must be present") {
    const std::string valid = manifest_with_pages(page("p00001", 1));
    CHECK(problem(replace_first(valid, "\"onf_version\":\"0.4.0\",", "")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"title\":\"T\",", "")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"created_at\":\"2026-06-14T10:32:00.000Z\",", "")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"modified_at\":\"2026-06-14T12:15:00.000Z\",", "")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(R"({"onf_version":"0.4.0","title":"T","created_at":"2026-06-14T10:32:00.000Z","modified_at":"2026-06-14T10:32:00.000Z"})") ==
          ErrorCode::ManifestInvalidField);
}

TEST_CASE("manifest: fields of the wrong type") {
    const std::string valid = manifest_with_pages(page("p00001", 1));
    CHECK(problem(replace_first(valid, "\"onf_version\":\"0.4.0\"", "\"onf_version\":2")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"title\":\"T\"", "\"title\":null")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"title\":\"T\"", "\"title\":5")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"created_at\":\"2026-06-14T10:32:00.000Z\"", "\"created_at\":1781433690000")) ==
          ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"modified_at\":\"2026-06-14T12:15:00.000Z\"", "\"modified_at\":true")) ==
          ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"pages\":[" + page("p00001", 1) + "]", "\"pages\":{}")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(replace_first(valid, "\"pages\":[" + page("p00001", 1) + "]", "\"pages\":\"p00001\"")) == ErrorCode::ManifestInvalidField);
}

TEST_CASE("manifest: malformed version and timestamps") {
    const std::string valid = manifest_with_pages(page("p00001", 1));
    for (const char* version : {"0.2", "0.4.0.1", "01.2.0", "0.4.0-draft", "", "v0.4.0", "0.4.0 "}) {
        CAPTURE(version);
        CHECK(problem(replace_first(valid, "\"0.4.0\"", std::string("\"") + version + "\"")) == ErrorCode::ManifestInvalidField);
    }
    for (const char* date : {"2026-06-14T10:32:00Z", "2026-06-14T10:32:00.000", "2026-02-30T10:32:00.000Z",
                             "2026-06-14T24:00:00.000Z", "2026-06-14T10:32:60.000Z", "2026-06-14 10:32:00.000Z", ""}) {
        CAPTURE(date);
        CHECK(problem(replace_first(valid, "2026-06-14T10:32:00.000Z", date)) == ErrorCode::ManifestInvalidField);
        CHECK(problem(replace_first(valid, "2026-06-14T12:15:00.000Z", date)) == ErrorCode::ManifestInvalidField);
    }
}

TEST_CASE("manifest: the number of pages") {
    CHECK(problem(manifest_with_pages("")) == ErrorCode::ManifestPagesInvalid);

    std::string too_many;
    for (int i = 0; i < 100000; ++i) {
        if (i > 0) too_many += ",";
        too_many += page("p00001", 1);
    }
    CHECK(problem(manifest_with_pages(too_many)) == ErrorCode::ManifestPagesInvalid);
}

TEST_CASE("manifest: the largest allowed manifest has 99999 pages") {
    std::string reversed;
    for (long i = 99999; i >= 1; --i) {
        char id[8];
        std::snprintf(id, sizeof id, "p%05ld", 100000 - i);
        if (i < 99999) reversed += ",";
        reversed += page(id, i);
    }
    const auto result = manifest_of(manifest_with_pages(reversed));
    REQUIRE(result.has_value());
    REQUIRE(result.value().pages.size() == 99999);
    CHECK(result.value().pages.front().id == "p99999");
    CHECK(result.value().pages.back().id == "p00001");
    CHECK(result.value().pages[1234].order == 1235);
}

TEST_CASE("manifest: page ids must be p00001 to pNNNNN, each once") {
    CHECK(problem(manifest_with_pages(page("p00001", 1) + "," + page("p00001", 2))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00001", 1) + "," + page("p00003", 2))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00002", 1) + "," + page("p00003", 2))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00000", 1))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00002", 1))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p99999", 1))) == ErrorCode::ManifestPagesInvalid);
}

TEST_CASE("manifest: page orders must be 1 to N, each once") {
    CHECK(problem(manifest_with_pages(page("p00001", 1) + "," + page("p00002", 1))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00001", 1) + "," + page("p00002", 3))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00001", 0))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00001", 2))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00001", -1))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00001", 1) + "," + page("p00002", 0))) == ErrorCode::ManifestPagesInvalid);
    CHECK(problem(manifest_with_pages(page("p00001", 9007199254740991))) == ErrorCode::ManifestPagesInvalid);
}

TEST_CASE("manifest: malformed page entries") {
    CHECK(problem(manifest_with_pages(page("P00001", 1))) == ErrorCode::InvalidIdentifier);
    CHECK(problem(manifest_with_pages(page("p0001", 1))) == ErrorCode::InvalidIdentifier);
    CHECK(problem(manifest_with_pages(page("p00001\\n", 1))) == ErrorCode::InvalidIdentifier);
    CHECK(problem(manifest_with_pages(page("../p0001", 1))) == ErrorCode::InvalidIdentifier);
    CHECK(problem(manifest_with_pages(R"({"order":1})")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(manifest_with_pages(R"({"id":"p00001"})")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(manifest_with_pages(R"({"id":1,"order":1})")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(manifest_with_pages(R"({"id":"p00001","order":"1"})")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(manifest_with_pages(R"({"id":"p00001","order":null})")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(manifest_with_pages(R"("p00001")")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(manifest_with_pages("1")) == ErrorCode::ManifestInvalidField);
    CHECK(problem(manifest_with_pages(R"([])")) == ErrorCode::ManifestInvalidField);
}

TEST_CASE("manifest: read from an archive") {
    const std::string manifest = manifest_with_pages(page("p00001", 2) + "," + page("p00002", 1));
    const auto result = read_from(archive_with_manifest(manifest, {"pages/p00001/meta.json", "pages/p00002/strokes.bin", "assets/"}));
    REQUIRE(result.has_value());
    CHECK(result.value().pages[0].id == "p00002");
}

TEST_CASE("manifest: page directories may be explicit entries or implied by files") {
    const std::string manifest = manifest_with_pages(page("p00001", 1) + "," + page("p00002", 2));
    CHECK(read_from(archive_with_manifest(manifest, {"pages/p00001/", "pages/p00002/"})).has_value());
    CHECK(read_from(archive_with_manifest(manifest, {"pages/p00001/meta.json", "pages/p00002/strokes.bin"})).has_value());
    CHECK(read_from(archive_with_manifest(manifest, {"pages/", "pages/p00001/", "pages/p00002/a/b/c"})).has_value());
}

TEST_CASE("manifest: a missing page directory rejects the file") {
    const std::string manifest = manifest_with_pages(page("p00001", 1) + "," + page("p00002", 2));

    const auto missing = read_from(archive_with_manifest(manifest, {"pages/p00001/meta.json"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::PageDirectoryMissing);
    CHECK(missing.error().entry == "pages/p00002/");

    CHECK(read_from(archive_with_manifest(manifest, {})).error().code == ErrorCode::PageDirectoryMissing);
    CHECK(read_from(archive_with_manifest(manifest, {"pages/p00001/", "pages/p00002"})).error().code == ErrorCode::PageDirectoryMissing);
    CHECK(read_from(archive_with_manifest(manifest, {"pages/p00001/", "pages/p00002x/f"})).error().code == ErrorCode::PageDirectoryMissing);
    CHECK(read_from(archive_with_manifest(manifest, {"pages/p00001/", "other/pages/p00002/f"})).error().code == ErrorCode::PageDirectoryMissing);
}

TEST_CASE("manifest: directories that the manifest does not mention are ignored") {
    const std::string manifest = manifest_with_pages(page("p00001", 1));
    const auto result = read_from(archive_with_manifest(manifest, {"pages/p00001/", "pages/p00002/meta.json", "pages/p00099/"}));
    CHECK(result.has_value());
}

TEST_CASE("manifest: a missing manifest.json rejects the file") {
    ZipSpec spec;
    spec.entries.push_back(entry("pages/p00001/meta.json", "{}"));
    const auto result = read_from(spec);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::ManifestMissing);

    CHECK(read_from(ZipSpec{}).error().code == ErrorCode::ManifestMissing);

    ZipSpec different_case;
    different_case.entries.push_back(entry("Manifest.json", manifest_with_pages(page("p00001", 1))));
    different_case.entries.push_back(entry("pages/p00001/"));
    CHECK(read_from(different_case).error().code == ErrorCode::ManifestMissing);

    ZipSpec nested;
    nested.entries.push_back(entry("x/manifest.json", manifest_with_pages(page("p00001", 1))));
    CHECK(read_from(nested).error().code == ErrorCode::ManifestMissing);
}

TEST_CASE("manifest: JSON errors in manifest.json reject the file with the JSON code") {
    struct Case {
        std::string json;
        ErrorCode code;
    };
    const std::vector<Case> cases = {
        {"{", ErrorCode::JsonSyntax},
        {"", ErrorCode::JsonSyntax},
        {"\xEF\xBB\xBF{}", ErrorCode::JsonBom},
        {R"({"onf_version":"0.4.0","onf_version":"0.4.0"})", ErrorCode::JsonDuplicateKey},
        {R"({"title":"a)" + std::string("\xC3") + R"("})", ErrorCode::JsonInvalidUtf8},
        {R"({"pages":[1.5]})", ErrorCode::JsonNonIntegerNumber},
        {std::string(33, '[') + std::string(33, ']'), ErrorCode::JsonNestingTooDeep},
    };
    for (const Case& c : cases) {
        CAPTURE(c.json);
        const auto result = read_from(archive_with_manifest(c.json, {}));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == c.code);
        CHECK(result.error().entry == "manifest.json");
    }
}

TEST_CASE("manifest: a manifest.json larger than 16 MiB is rejected before it is read") {
    ZipEntrySpec big = entry("manifest.json", "{}");
    big.method = 8;
    big.declared_size = limits::kMaxManifestSize + 1;
    const std::size_t needed_for_ratio = (*big.declared_size + 99) / 100;
    big.extra_payload_bytes = needed_for_ratio - deflate_raw(big.data).size();
    ZipSpec spec;
    spec.entries.push_back(big);

    const auto result = read_from(spec);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::ManifestTooLarge);
}

TEST_CASE("manifest: a manifest.json of exactly 16 MiB is read") {
    std::string text = manifest_with_pages(page("p00001", 1));
    // A random block larger than the 32 KiB deflate window, so the padding does not compress like zeros.
    std::mt19937_64 rng(2024);
    const char whitespace[] = {' ', '\n', '\t'};
    std::string block(64 * 1024, ' ');
    for (char& c : block) c = whitespace[rng() % 3];
    while (text.size() + block.size() <= limits::kMaxManifestSize) text += block;
    text.append(block, 0, limits::kMaxManifestSize - text.size());
    REQUIRE(text.size() == limits::kMaxManifestSize);

    ZipEntrySpec big = entry("manifest.json", text);
    big.method = 8;
    big.deflate_level = 1;
    ZipSpec spec;
    spec.entries.push_back(big);
    spec.entries.push_back(entry("pages/p00001/"));

    const auto result = read_from(spec);
    REQUIRE_MESSAGE(result.has_value(), error_name(result.error().code) << ": " << result.error().message);
    CHECK(result.value().pages.size() == 1);
}

TEST_CASE("manifest: manifests from a mutated valid file never crash") {
    const std::string valid = kSpecExample;
    std::mt19937_64 rng(31337);
    for (int round = 0; round < 3000; ++round) {
        std::string mutated = valid;
        const int flips = 1 + static_cast<int>(rng() % 3);
        for (int f = 0; f < flips; ++f) mutated[rng() % mutated.size()] = static_cast<char>(rng());
        auto json = parse_json(mutated);
        if (json.has_value()) static_cast<void>(parse_manifest(json.value()));
    }
}

TEST_CASE("manifest: generator is optional, informational and a string") {
    const std::string valid = manifest_with_pages(page("p00001", 1));
    CHECK_FALSE(manifest_of(valid).value().generator.has_value());

    const std::string with_generator = replace_first(valid, "\"title\":\"T\",", "\"generator\":\"onf-tools 0.3.0\",\"title\":\"T\",");
    CHECK(manifest_of(with_generator).value().generator == "onf-tools 0.3.0");

    // An empty string is a string; the content is never interpreted.
    CHECK(manifest_of(replace_first(valid, "\"title\":\"T\",", "\"generator\":\"\",\"title\":\"T\",")).value().generator == "");

    for (const char* bad : {"5", "null", "true", "[]", "{}"}) {
        CAPTURE(bad);
        CHECK(problem(replace_first(valid, "\"title\":\"T\",", std::string("\"generator\":") + bad + ",\"title\":\"T\",")) ==
              ErrorCode::ManifestInvalidField);
    }

    // The string limit of spec 12.6 applies (65535 code points).
    const auto too_long = parse_json(replace_first(valid, "\"title\":\"T\",", "\"generator\":\"" + std::string(65536, 'x') + "\",\"title\":\"T\","));
    REQUIRE_FALSE(too_long.has_value());
    CHECK(too_long.error().code == ErrorCode::JsonStringTooLong);
}
