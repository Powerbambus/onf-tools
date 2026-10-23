#pragma once

// Text objects and asset placements of meta.json, spec 8 and 9.3.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "onf/model/types.h"

namespace onf {

// Style override for a range of code points, spec 8.3.1. An empty optional keeps the text object's default.
struct Span {
    std::uint32_t start = 0;  // code point index
    std::uint32_t end = 0;    // exclusive, greater than start
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<bool> strikethrough;
    std::optional<Color> color;
    std::optional<std::int32_t> font_size;  // positive
    std::optional<std::string> font_family;
    std::optional<std::string> generic_family;  // present whenever font_family is
};

struct TextObject {
    std::string id;
    std::uint16_t layer = 0;           // 1 to 65535, spec 5.6
    std::optional<MsTime> created_at;  // absent means unknown
    Coord x = 0;
    Coord y = 0;
    Coord width = 0;   // positive
    Coord height = 0;  // positive
    std::string content;
    std::string font_family;
    std::string generic_family;  // as written, an unknown value falls back to sans-serif when rendering (spec 8.4.1)
    std::int32_t font_size = 0;  // positive
    Color color;
    bool bold = false;
    bool italic = false;
    std::vector<Span> spans;
};

// The region of an image that is shown, spec 9.3.2 and 9.4.5. Pixels of the decoded image, origin top left.
// The member values are within their ranges (x, y at least 0, width, height at least 1, all at most 2^31 - 1).
// That the region lies inside the image is only known once the image header is read.
struct Crop {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 1;
    std::int32_t height = 1;

    friend bool operator==(const Crop&, const Crop&) = default;
};

struct AssetRef {
    std::string id;
    std::uint16_t layer = 0;
    std::optional<MsTime> created_at;
    std::string hash;       // 64 lowercase hex digits
    std::string extension;  // as written, may be an extension this version does not know
    std::optional<std::int32_t> pdf_page;  // set for extension "pdf", first page is 1
    std::optional<Crop> crop;              // images only; a crop on a pdf placement is ignored (spec 9.3.2)
    Coord x = 0;
    Coord y = 0;
    Coord width = 0;   // positive
    Coord height = 0;  // positive
};

}  // namespace onf
