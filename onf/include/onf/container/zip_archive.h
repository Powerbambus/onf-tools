#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/errors.h"
#include "onf/result.h"

namespace onf {

struct ZipEntryInfo {
    std::string name;  // validated against spec 4.6
    std::uint64_t size = 0;             // declared uncompressed size
    std::uint64_t compressed_size = 0;  // declared compressed size
    bool is_directory = false;
};

// A validated ONF container (spec 4.1, 4.5, 4.6, 4.7). open() rejects the whole archive on any violation.
class ZipArchive {
public:
    static Result<ZipArchive, FileError> open(ByteSource source);

    ZipArchive(ZipArchive&&) noexcept;
    ZipArchive& operator=(ZipArchive&&) noexcept;
    ~ZipArchive();

    const std::vector<ZipEntryInfo>& entries() const noexcept;
    std::optional<std::size_t> find(std::string_view name) const;

    // Decompresses one entry. Never produces more bytes than the entry declares.
    Result<std::vector<std::uint8_t>, FileError> read(std::size_t index) const;

private:
    struct Impl;
    explicit ZipArchive(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace onf
