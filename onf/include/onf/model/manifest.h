#pragma once

#include <cstdint>
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
    std::string title;
    UtcDateTime created_at;
    UtcDateTime modified_at;
    std::vector<PageEntry> pages;  // sorted by order, which runs from 1 to pages.size()
};

}  // namespace onf
