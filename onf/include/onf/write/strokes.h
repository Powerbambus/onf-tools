#pragma once

#include <cstdint>
#include <vector>

#include "onf/model/stroke.h"

namespace onf {

// strokes.bin of spec 7: file header, one chunk per ink item in the given order, and the END chunk. The ink must have
// consistent sample columns (as the parser returns them); the value ranges are not checked here, check_writable does
// that. Anything a reader returns for a file encodes to a file that reads back to the same ink.
std::vector<std::uint8_t> encode_strokes_file(const std::vector<InkItem>& ink);

}  // namespace onf
