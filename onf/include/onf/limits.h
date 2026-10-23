#pragma once

// Limits and fixed sizes from the specification, with the section of each group.

#include <cstdint>

namespace onf::limits {

// 4.7
inline constexpr std::uint64_t kMaxEntryUncompressedSize = 2147483648ull;      // 2 GiB
inline constexpr std::uint64_t kMaxTotalUncompressedSize = 10737418240ull;     // 10 GiB
inline constexpr std::uint64_t kMaxCompressionRatio = 100;                     // 100:1, uncompressed/compressed
// Project decision, not in the spec: the ratio is only checked from this declared size on.
inline constexpr std::uint64_t kRatioCheckMinSize = 1048576ull;                // 1 MiB

static_assert(kMaxEntryUncompressedSize == (1ull << 31));
static_assert(kMaxTotalUncompressedSize == 10ull * (1ull << 30));
static_assert(kRatioCheckMinSize == (1ull << 20));

// 4.2, 6.1.2
inline constexpr std::uint32_t kMaxPages = 99999;

// Project decision: spec 12.6 allows stricter limits on the file size. 99999 pages need about 5 MB.
inline constexpr std::uint64_t kMaxManifestSize = 16ull * 1024 * 1024;

// Project decision: a thumbnail is at most 256 pixels wide (spec 4.3), so 16 MiB is far more than needed.
inline constexpr std::uint64_t kMaxThumbnailSize = 16ull * 1024 * 1024;

// 12.6, counted in code points
inline constexpr std::uint32_t kJsonMaxDepth = 32;                   // top-level value has depth 1
inline constexpr std::uint32_t kJsonMaxStringCodePoints = 65535;
inline constexpr std::uint32_t kJsonMaxContentCodePoints = 1048576;  // text object `content` only

// 2.3.5
inline constexpr std::int64_t kJsonMaxInteger = 9007199254740991ll;

static_assert(kJsonMaxContentCodePoints == (1u << 20));
static_assert(kJsonMaxInteger == (1ll << 53) - 1);

// 2.3.2
inline constexpr unsigned kVarintMaxBytes = 5;
inline constexpr std::uint64_t kVarintMaxValue = 8589934591ull;      // 2^33 - 1, zigzag fields
inline constexpr std::uint64_t kTimestampMaxValue = 4294967295ull;   // 2^32 - 1, timestamp field

static_assert(kVarintMaxValue == (1ull << 33) - 1);
static_assert(kTimestampMaxValue == (1ull << 32) - 1);

// 7.2, 7.3, 7.3.2
inline constexpr std::uint64_t kStrokesFileHeaderSize = 8;
inline constexpr std::uint64_t kChunkFrameSize = 6;
inline constexpr std::uint64_t kStrokeHeaderSize = 20;        // 7.4.1, without created_at
inline constexpr std::uint64_t kCreatedAtSize = 8;            // 7.4.1, 7.9.2, only with HAS_CREATED_AT
inline constexpr std::uint64_t kShapeHeaderSize = 16;         // 7.9.2, without created_at and shape_type
inline constexpr std::uint64_t kShapeTypeSize = 2;            // 7.9.2
inline constexpr std::uint64_t kMinSampleSize = 3;            // x, y, pressure
inline constexpr std::uint64_t kTimestampMinSize = 1;         // the timestamp field, unless NO_TIMESTAMP is set

// 7.9.4: size of the parameters of each shape type
inline constexpr std::uint64_t kEllipseParameterSize = 18;
inline constexpr std::uint64_t kRectangleParameterSize = 18;
inline constexpr std::uint64_t kLineParameterSize = 16;
inline constexpr std::uint64_t kTriangleParameterSize = 24;

// 7.9.6
inline constexpr std::uint32_t kMaxRotation = 35999;  // hundredths of a degree

// 5.6
inline constexpr std::uint32_t kMinLayer = 1;
inline constexpr std::uint32_t kMaxLayer = 65535;

// 8.3.1
inline constexpr std::uint32_t kMaxSpans = 65535;

// 9.3.2
inline constexpr std::int64_t kMaxPdfPage = 2147483647;
inline constexpr std::int64_t kMaxCropValue = 2147483647;

}  // namespace onf::limits
