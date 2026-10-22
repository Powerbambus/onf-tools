#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "onf/errors.h"
#include "onf/primitives/bytecursor.h"
#include "onf/result.h"

namespace onf {

// The bytes of an archive: a file or memory the source owns. Not thread-safe.
class ByteSource {
public:
    static Result<ByteSource, FileError> from_file(const std::filesystem::path& path);
    static ByteSource from_memory(std::vector<std::uint8_t> bytes);

    ByteSource(ByteSource&&) noexcept = default;
    ByteSource& operator=(ByteSource&&) noexcept = default;

    std::uint64_t size() const noexcept { return size_; }
    Result<void, ReadError> read_at(std::uint64_t offset, std::span<std::uint8_t> out) const;

    // Exactly one of the two is set: the path of a file source, or the bytes of a memory source.
    const std::filesystem::path* file_path() const noexcept { return is_file_ ? &path_ : nullptr; }
    std::span<const std::uint8_t> memory() const noexcept { return bytes_; }

private:
    ByteSource() = default;

    bool is_file_ = false;
    std::filesystem::path path_;
    std::vector<std::uint8_t> bytes_;
    mutable std::ifstream stream_;
    std::uint64_t size_ = 0;
};

}  // namespace onf
