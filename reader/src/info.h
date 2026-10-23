#pragma once

#include <ostream>

#include "onf/diagnostics.h"
#include "onf/read/document.h"

namespace onf_reader {

// Prints the manifest and a summary of every page. Pages that cannot be read are listed with their error; they do
// not stop the output (spec 12.3.1). Returns the number of unreadable pages.
int print_info(const onf::Document& document, std::ostream& out, onf::DiagnosticsSink* sink);

// "code name (spec X): message"
std::string describe_error(onf::ErrorCode code, const std::string& message);

}  // namespace onf_reader
