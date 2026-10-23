#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "onf/model/types.h"
#include "onf/model/version.h"

namespace onf {

struct PageEntry {
    std::string id;  // "p00001"
    std::uint32_t order = 0;
};

struct Manifest {
    Version onf_version;
    std::optional<std::string> generator;  // informational only, spec 6.1.2
    std::string title;
    UtcDateTime created_at;
    UtcDateTime modified_at;
    std::vector<PageEntry> pages;  // sorted by order, which runs from 1 to pages.size()
};

}  // namespace onf
