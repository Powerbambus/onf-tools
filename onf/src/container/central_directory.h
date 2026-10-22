#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/errors.h"
#include "onf/result.h"

namespace onf {

struct CentralEntry {
    std::string name;  // raw bytes from the archive, may contain NUL
    std::uint16_t flags = 0;
};

// Reads the end of central directory record (with ZIP64) and the central directory directly. It checks what
// libzip hides or ignores: multi-disk archives, NUL bytes in names, encrypted entries and duplicate names.
Result<std::vector<CentralEntry>, FileError> scan_central_directory(const ByteSource& source);

}  // namespace onf
