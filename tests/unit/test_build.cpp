#include "doctest.h"

// UBSan has no compile-time detection macro, so only ASan is checked.
#if defined(ONF_EXPECT_SANITIZERS)
TEST_CASE("AddressSanitizer is active in the asan-ubsan preset") {
#if defined(__SANITIZE_ADDRESS__)
    CHECK(true);
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
    CHECK(true);
#else
    FAIL("AddressSanitizer is not active although ONF_SANITIZE is ON");
#endif
#else
    FAIL("AddressSanitizer is not active although ONF_SANITIZE is ON");
#endif
}
#endif
