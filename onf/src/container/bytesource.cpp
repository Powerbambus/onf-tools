#include "onf/container/bytesource.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace onf {

Result<ByteSource, FileError> ByteSource::from_file(const std::filesystem::path& path) {
    ByteSource source;
    source.is_file_ = true;
    source.path_ = path;

    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return unexpected(FileError{ErrorCode::ZipOpenFailed, "cannot determine the file size: " + ec.message(), {}});
    source.size_ = size;

    source.stream_.open(path, std::ios::binary);
    if (!source.stream_) return unexpected(FileError{ErrorCode::ZipOpenFailed, "cannot open the file", {}});
    return source;
}

ByteSource ByteSource::from_memory(std::vector<std::uint8_t> bytes) {
    ByteSource source;
    source.size_ = bytes.size();
    source.bytes_ = std::move(bytes);
    return source;
}

Result<void, ReadError> ByteSource::read_at(std::uint64_t offset, std::span<std::uint8_t> out) const {
    if (offset > size_ || out.size() > size_ - offset) return unexpected(ReadError{});
    if (out.empty()) return {};

    if (!is_file_) {
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
        return {};
    }

    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset));
    stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!stream_ || static_cast<std::size_t>(stream_.gcount()) != out.size()) return unexpected(ReadError{});
    return {};
}

}  // namespace onf
