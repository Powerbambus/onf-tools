#pragma once

#include <string>

#include "onf/model/manifest.h"
#include "onf/model/page.h"
#include "onf/read/json.h"
#include "onf/write/writer.h"

namespace onf {

// "YYYY-MM-DDTHH:MM:SS.mmmZ" (spec 6.1.2).
std::string format_utc_datetime(const UtcDateTime& time);

// "#RRGGBBAA" in upper case (spec 2.3.5).
std::string format_color(const Color& color);

// "p00001" for index 1 (spec 4.2).
std::string page_directory_name(std::size_t index);

JsonValue manifest_to_json(const WriteDocument& document);
JsonValue page_meta_to_json(const PageMeta& meta);

}  // namespace onf
