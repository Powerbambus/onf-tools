#pragma once

#include <string_view>

#include "onf/container/zip_archive.h"
#include "onf/errors.h"
#include "onf/model/page.h"
#include "onf/read/json.h"
#include "onf/result.h"

namespace onf {

// The rules of spec 6.2 and 6.3.1. Unknown keys and unknown background types are not errors.
// Errors: MetaInvalidField, BackgroundInvalidField.
Result<PageMeta, PageError> parse_page_meta(const JsonValue& root, std::string_view page_id);

// pages/<page_id>/meta.json of the archive. Errors: PageFileMissing, JSON errors and the errors above.
Result<PageMeta, PageError> read_page_meta(const ZipArchive& archive, std::string_view page_id);

}  // namespace onf
