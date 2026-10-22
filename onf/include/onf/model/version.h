#pragma once

#include <cstdint>

namespace onf {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    friend bool operator==(const Version&, const Version&) = default;
};

}  // namespace onf
