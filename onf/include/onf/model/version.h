#pragma once

#include <cstdint>
#include <string>

namespace onf {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    friend bool operator==(const Version&, const Version&) = default;
};

inline std::string to_string(const Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

}  // namespace onf
