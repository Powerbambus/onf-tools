#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "onf/model/content.h"
#include "onf/model/stroke.h"
#include "onf/model/types.h"

namespace onf {

enum class BackgroundKind { Blank, Ruled, Grid, Dotted, Unknown };

struct Background {
    std::string type;  // as written in the file
    BackgroundKind kind = BackgroundKind::Unknown;
    Color color;
    std::optional<Color> foreground_color;  // set for ruled, grid and dotted
    std::optional<Coord> spacing;           // set for ruled, grid and dotted, always positive
};

struct PageMeta {
    Coord width = 0;   // always positive
    Coord height = 0;  // always positive
    Background background;
    std::vector<TextObject> text_objects;  // in file order, ids are unique
    std::vector<AssetRef> assets;          // in file order, ids are unique
};

// One fully read page: strokes and shapes in the order of the file.
struct Page {
    std::string id;
    PageMeta meta;
    std::vector<InkItem> ink;
};

}  // namespace onf
