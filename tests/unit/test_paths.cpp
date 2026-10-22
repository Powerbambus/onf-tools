#include "doctest.h"

#include <string>
#include <string_view>
#include <vector>

#include "onf/container/paths.h"

using namespace onf;

namespace {

ErrorCode problem(std::string_view name) {
    const auto result = validate_entry_name(name);
    REQUIRE_MESSAGE(!result.has_value(), "unexpectedly accepted: [" << name << "]");
    return result.error();
}

std::string with(char c, std::string_view before = "a", std::string_view after = "b") {
    return std::string(before) + c + std::string(after);
}

}  // namespace

TEST_CASE("paths: names used by the format are accepted") {
    for (std::string_view name : {"manifest.json", "pages/", "pages/p00001/", "pages/p00001/meta.json",
                                  "pages/p00001/strokes.bin", "assets/", "assets/a3f1c2d4.webp", "thumbnail.png"}) {
        CAPTURE(name);
        CHECK(validate_entry_name(name).has_value());
    }
}

TEST_CASE("paths: harmless unusual names are accepted") {
    for (std::string_view name : {"a..b", "..a", "a..", "...", ".hidden", "dir/.hidden/x", "a b", "a-b_c.d", "ab:c",
                                  "1:x", "C", "dir/", "x/y/z/"}) {
        CAPTURE(name);
        CHECK(validate_entry_name(name).has_value());
    }
    CHECK(validate_entry_name(std::string("caf") + "\xC3\xA9" + "/x").has_value());
}

TEST_CASE("paths: traversal and absolute paths are rejected") {
    for (std::string_view name : {"..", "../x", "a/..", "a/../b", "../../etc/passwd", "a/b/../../..", "/", "/abs",
                                  "/etc/passwd", "//server/share", "///"}) {
        CAPTURE(name);
        CHECK(problem(name) == ErrorCode::UnsafePath);
    }
}

TEST_CASE("paths: backslashes, drive letters and UNC prefixes are rejected") {
    for (std::string_view name : {"a\\b", "\\", "\\a", "..\\x", "C:\\x", "C:\\", "\\\\server\\share", "a/b\\c"}) {
        CAPTURE(name);
        CHECK(problem(name) == ErrorCode::UnsafePath);
    }
    for (std::string_view name : {"C:", "c:", "z:/x", "Z:x", "C:file"}) {
        CAPTURE(name);
        CHECK(problem(name) == ErrorCode::UnsafePath);
    }
}

TEST_CASE("paths: empty, dot and empty segments are rejected") {
    for (std::string_view name : {"", ".", "./a", "a/./b", "a/.", "a//b", "a//", "//", "a/b//", "assets//x", "/a/b"}) {
        CAPTURE(name);
        CHECK(problem(name) == ErrorCode::UnsafePath);
    }
}

TEST_CASE("paths: NUL and control characters are rejected") {
    CHECK(problem(std::string_view("a\0b", 3)) == ErrorCode::UnsafePath);
    CHECK(problem(std::string_view("\0", 1)) == ErrorCode::UnsafePath);
    CHECK(problem(std::string_view("pages/p00001/meta.json\0", 23)) == ErrorCode::UnsafePath);
    for (int code = 1; code < 0x20; ++code) {
        CAPTURE(code);
        CHECK(problem(with(static_cast<char>(code))) == ErrorCode::UnsafePath);
    }
    CHECK(problem(with('\x7F')) == ErrorCode::UnsafePath);
    CHECK(problem(with('\n')) == ErrorCode::UnsafePath);
    CHECK(validate_entry_name(with(' ')).has_value());
}

TEST_CASE("paths: names that are not valid UTF-8 get their own error") {
    CHECK(problem("\xC3") == ErrorCode::EntryNameInvalidUtf8);
    CHECK(problem("\xFF") == ErrorCode::EntryNameInvalidUtf8);
    CHECK(problem(std::string("a") + "\xC0\x80") == ErrorCode::EntryNameInvalidUtf8);
    CHECK(problem(std::string("a/") + "\xED\xA0\x80") == ErrorCode::EntryNameInvalidUtf8);
    CHECK(problem(std::string("../") + "\xFF") == ErrorCode::EntryNameInvalidUtf8);
}

TEST_CASE("identifiers: page ids") {
    for (std::string_view id : {"p00001", "p00002", "p99999", "p00000", "p12345"}) {
        CAPTURE(id);
        CHECK(is_valid_page_id(id));
    }
    for (std::string_view id : {"", "p", "p0001", "p000001", "P00001", "p0000a", "00001", "p-0001", "p+0001",
                                " p00001", "p00001 ", "p00001\n", "p 0001", "q00001", "pp0001"}) {
        CAPTURE(id);
        CHECK_FALSE(is_valid_page_id(id));
    }
    CHECK_FALSE(is_valid_page_id(std::string_view("p00001\0", 7)));
    CHECK_FALSE(is_valid_page_id(std::string("p0000") + "\xD9\xA1"));
}

TEST_CASE("identifiers: asset hashes") {
    const std::string valid(64, 'a');
    CHECK(is_valid_asset_hash(valid));
    CHECK(is_valid_asset_hash("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));

    CHECK_FALSE(is_valid_asset_hash(""));
    CHECK_FALSE(is_valid_asset_hash(std::string(63, 'a')));
    CHECK_FALSE(is_valid_asset_hash(std::string(65, 'a')));
    CHECK_FALSE(is_valid_asset_hash(std::string(63, 'a') + "A"));
    CHECK_FALSE(is_valid_asset_hash(std::string(63, 'a') + "g"));
    CHECK_FALSE(is_valid_asset_hash(std::string(63, 'a') + "\n"));
    CHECK_FALSE(is_valid_asset_hash(std::string(63, 'a') + '\0'));
    CHECK_FALSE(is_valid_asset_hash(std::string(" ") + std::string(63, 'a')));
    CHECK_FALSE(is_valid_asset_hash(std::string(62, 'a') + "../"));
}

TEST_CASE("identifiers: only webp, png and jpg are supported extensions") {
    CHECK(is_supported_asset_extension("webp"));
    CHECK(is_supported_asset_extension("png"));
    CHECK(is_supported_asset_extension("jpg"));
    for (std::string_view extension : {"", "jpeg", "gif", "svg", "bmp", "heic", "avif", "PNG", "Webp", "JPG", ".png",
                                       "png ", "png\n", "webp/", "../png", "pn"}) {
        CAPTURE(extension);
        CHECK_FALSE(is_supported_asset_extension(extension));
    }
}
