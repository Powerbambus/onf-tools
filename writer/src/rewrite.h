#pragma once

#include <string>
#include <vector>

#include "onf/diagnostics.h"
#include "onf/read/document.h"
#include "onf/write/writer.h"

namespace onf_writer {

struct RewriteError {
    onf::ErrorCode code;
    std::string message;
};

// Reads every page and every asset of an opened document into a WriteDocument. All pages are renumbered p00001 and
// up in display order. Nothing is dropped: a page that cannot be read, or a placement whose asset file is missing,
// unsupported or corrupt, is an error, because the result could not be a faithful copy.
//
// The one exception is the thumbnail: it is only a preview, and one that is not a valid PNG of at most 256 pixels
// (spec 4.3) cannot be written. It is left out and the reason is appended to `notes`.
onf::Result<onf::WriteDocument, RewriteError> collect_document(const onf::Document& document, onf::DiagnosticsSink* sink,
                                                                 std::vector<std::string>& notes);

}  // namespace onf_writer
