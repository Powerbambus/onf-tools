#include "doctest.h"

#include "onf/limits.h"

TEST_CASE("limits are consistent") {
    using namespace onf::limits;
    CHECK(kMaxEntryUncompressedSize < kMaxTotalUncompressedSize);
    CHECK(kJsonMaxStringCodePoints < kJsonMaxContentCodePoints);
    CHECK(kTimestampMaxValue < kVarintMaxValue);
    CHECK(kStrokeHeaderSize > kStrokesFileHeaderSize);
}

TEST_CASE("sizes of the strokes.bin records (spec 7.4.1, 7.9.2, 7.9.4)") {
    using namespace onf::limits;
    CHECK(kStrokeHeaderSize == 20);
    CHECK(kCreatedAtSize == 8);
    CHECK(kShapeHeaderSize == 16);
    CHECK(kShapeTypeSize == 2);
    CHECK(kMinSampleSize == 3);
    CHECK(kMinSampleSize + kTimestampMinSize == 4);
    CHECK(kEllipseParameterSize == 4 * 4 + 2);
    CHECK(kRectangleParameterSize == 4 * 4 + 2);
    CHECK(kLineParameterSize == 4 * 4);
    CHECK(kTriangleParameterSize == 6 * 4);
    CHECK(kMaxRotation == 35999);
    CHECK(kMinLayer == 1);
    CHECK(kMaxLayer == 65535);
    CHECK(kMaxSpans == 65535);
    CHECK(kMaxPdfPage == 2147483647);
}
