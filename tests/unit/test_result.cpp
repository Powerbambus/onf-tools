#include "doctest.h"

#include <string>

#include "onf/result.h"

using onf::Result;
using onf::unexpected;

TEST_CASE("Result holds a value") {
    Result<int, std::string> r = 42;
    CHECK(r.has_value());
    CHECK(static_cast<bool>(r));
    CHECK(r.value() == 42);
    CHECK(*r == 42);
}

TEST_CASE("Result holds an error") {
    Result<int, std::string> r = unexpected(std::string("boom"));
    CHECK_FALSE(r.has_value());
    CHECK_FALSE(static_cast<bool>(r));
    CHECK(r.error() == "boom");
}

TEST_CASE("Result works when value and error type are the same") {
    Result<std::string, std::string> ok = std::string("value");
    Result<std::string, std::string> bad = unexpected(std::string("error"));
    CHECK(ok.has_value());
    CHECK(ok.value() == "value");
    CHECK_FALSE(bad.has_value());
    CHECK(bad.error() == "error");
}

TEST_CASE("Result moves the value out of an rvalue") {
    Result<std::string, int> r = std::string("a long string that does not fit into small string optimisation");
    std::string moved = std::move(r).value();
    CHECK(moved.size() > 30);
}

TEST_CASE("Result<void, E>") {
    Result<void, std::string> ok;
    CHECK(ok.has_value());

    Result<void, std::string> bad = unexpected(std::string("nope"));
    CHECK_FALSE(bad.has_value());
    CHECK(bad.error() == "nope");
}
