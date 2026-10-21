#include "doctest.h"

#include <set>
#include <string>

#include "onf/errors.h"

TEST_CASE("every error code has a unique number, a unique name and a spec reference") {
    std::set<unsigned> numbers;
    std::set<std::string> names;

    const auto table = onf::error_table();
    CHECK(table.size() > 0);

    for (const onf::ErrorInfo& info : table) {
        CAPTURE(info.name);
        CHECK(numbers.insert(static_cast<unsigned>(info.code)).second);
        CHECK(names.insert(std::string(info.name)).second);
        CHECK_FALSE(info.name.empty());
        CHECK_FALSE(info.spec_ref.empty());
        for (char c : info.spec_ref) CHECK((c == '.' || (c >= '0' && c <= '9')));
    }
}

TEST_CASE("error code lookup") {
    CHECK(onf::error_name(onf::ErrorCode::VarintNotCanonical) == "varint_not_canonical");
    CHECK(onf::error_spec_ref(onf::ErrorCode::VarintNotCanonical) == "2.3.2");
    CHECK(onf::error_name(static_cast<onf::ErrorCode>(60000)) == "unknown");
    CHECK(onf::error_spec_ref(static_cast<onf::ErrorCode>(60000)).empty());
}

TEST_CASE("error code numbers are stable") {
    CHECK(static_cast<unsigned>(onf::ErrorCode::ZipOpenFailed) == 101);
    CHECK(static_cast<unsigned>(onf::ErrorCode::StrokesBadMagic) == 402);
    CHECK(static_cast<unsigned>(onf::ErrorCode::VarintNotCanonical) == 504);
}
