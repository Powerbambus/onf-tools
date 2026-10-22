#pragma once

#include <cstdint>
#include <optional>
#include <string>

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

// text_objects and assets are checked to be arrays but not read yet.
struct PageMeta {
    Coord width = 0;   // always positive
    Coord height = 0;  // always positive
    Background background;
};

}  // namespace onf
