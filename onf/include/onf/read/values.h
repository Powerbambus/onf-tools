#pragma once

#include <optional>
#include <string_view>

#include "onf/model/types.h"
#include "onf/model/version.h"

namespace onf {

// "#RRGGBBAA", hex digits in either case (spec 5.2, 6.3.1). Nothing else is accepted.
std::optional<Color> parse_color(std::string_view text);

// "MAJOR.MINOR.PATCH" (spec 10.1). Components have no leading zeros and fit into 32 bits.
std::optional<Version> parse_version(std::string_view text);

// "YYYY-MM-DDTHH:MM:SS.mmmZ" with calendar validity (spec 6.1.2). Second 60 is rejected.
std::optional<UtcDateTime> parse_utc_datetime(std::string_view text);

}  // namespace onf
