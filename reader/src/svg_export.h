#pragma once

#include <string>

#include "onf/diagnostics.h"
#include "onf/read/document.h"

namespace onf_reader {

// Draws one page as an SVG document in the order of spec 3.4: background, then the layers in ascending order, and in
// each layer the assets, the text objects and the ink. Strokes follow the tool rules of spec 7.7, shapes spec 7.9.5,
// line erasers are masks over the earlier ink of their layer (spec 7.4.4).
//
// Limits of this export, none of them hides content silently:
//  - text is not wrapped (spec 8.6.3) and the font is chosen by the viewer, so no font substitution event is raised;
//  - a PDF placement is drawn as a labelled placeholder, because there is no PDF renderer;
//  - pencil strokes vary their opacity per segment, so overlapping segments of one stroke darken each other.
// Asset problems (missing, unsupported, corrupt) are reported to the sink and drawn as an error placeholder.
std::string export_page_svg(const onf::Document& document, const onf::Page& page, onf::DiagnosticsSink* sink);

}  // namespace onf_reader
