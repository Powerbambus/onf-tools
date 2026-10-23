#pragma once

// Erasing ink, spec 7.6.1. The file stores no erase information: the samples that lie in the erased area are removed,
// and what remains of a stroke becomes one or more new strokes. This is an operation for applications; readers never
// need it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "onf/model/stroke.h"

namespace onf {

// The area that the eraser covers, in coord units (spec 5.1). The position is not rounded to whole coord units
// because a pointer rarely is on one.
struct EraseDisk {
    double x = 0;
    double y = 0;
    double radius = 0;  // not negative
};

struct EraseSummary {
    std::size_t strokes_cut = 0;      // strokes that lost samples and remain, whole or in several parts
    std::size_t strokes_removed = 0;  // strokes without a remaining part
    std::size_t shapes_replaced = 0;  // shapes whose remaining outline is now stroked as strokes (spec 7.6.1)
    std::size_t shapes_removed = 0;   // shapes without a remaining part

    bool changed() const noexcept { return strokes_cut + strokes_removed + shapes_replaced + shapes_removed != 0; }
};

// Erases the part of the ink of `layer` that lies inside the disk. Ink on other layers is left alone.
//
// An object is touched if the disk reaches its path or comes within half of its `base_width` of it, because the
// width is part of what the user sees. For a stroke the samples inside that area are removed, and a sample is added
// on each border of the area where the path of the stroke enters or leaves it (position, pressure and elapsed time are
// interpolated, tilt and azimuth are taken from the nearer neighbour). The first remaining part keeps the
// `stroke_id`, the other parts get unused ones. A cut end of a fountain pen stroke gets `NO_TAPER_START` or
// `NO_TAPER_END`. A remaining part with a single sample would only be a dot at the border and is dropped, unless the
// stroke had a single sample to begin with.
//
// A shape that is not touched stays a shape. A touched shape is replaced by strokes that follow its outline
// (an ellipse as a polygon of 96 corners), and those are cut like any stroke. The parts take the tool, color, width,
// layer and `created_at` of the shape.
//
// The order of the ink is kept: the parts take the place of the object they come from.
EraseSummary erase_disk(std::vector<InkItem>& ink, std::uint16_t layer, const EraseDisk& disk);

}  // namespace onf
