#include "doctest.h"

#include "onf/limits.h"

TEST_CASE("limits are consistent") {
    using namespace onf::limits;
    CHECK(kMaxEntryUncompressedSize < kMaxTotalUncompressedSize);
    CHECK(kJsonMaxStringCodePoints < kJsonMaxContentCodePoints);
    CHECK(kTimestampMaxValue < kVarintMaxValue);
    CHECK(kStrokeHeaderSize > kStrokesFileHeaderSize);
}
