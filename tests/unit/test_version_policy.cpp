#include "doctest.h"

#include <string>
#include <vector>

#include "onf/read/json.h"
#include "onf/read/manifest.h"
#include "onf/read/values.h"
#include "onf/read/version_policy.h"

using namespace onf;

namespace {

VersionStatus status_of(const char* text) {
    const auto version = parse_version(text);
    REQUIRE_MESSAGE(version.has_value(), text);
    return classify_version(*version);
}

}  // namespace

TEST_CASE("version: 0.3.x is supported") {
    for (const char* text : {"0.3.0", "0.3.1", "0.3.99", "0.3.4294967295"}) {
        CAPTURE(text);
        CHECK(status_of(text) == VersionStatus::Supported);
    }
}

TEST_CASE("version: every other 0.x is an unsupported pre-release") {
    for (const char* text : {"0.0.0", "0.0.1", "0.1.0", "0.1.4", "0.2.0", "0.2.1", "0.4.0", "0.10.0", "0.20.0", "0.12.0",
                             "0.4294967295.0"}) {
        CAPTURE(text);
        CHECK(status_of(text) == VersionStatus::UnsupportedPreRelease);
    }
}

TEST_CASE("version: MAJOR 1 and higher is a newer major version") {
    for (const char* text : {"1.0.0", "1.2.0", "1.5.3", "2.0.0", "10.2.0", "4294967295.0.0", "4294967295.2.0"}) {
        CAPTURE(text);
        CHECK(status_of(text) == VersionStatus::NewerMajor);
    }
}

TEST_CASE("version: events") {
    const Version old_draft{0, 1, 4};
    const Version newer{1, 0, 0};

    CollectingSink sink;
    report_version_event({0, 2, 7}, VersionStatus::Supported, &sink);
    CHECK(sink.items().empty());

    report_version_event(old_draft, VersionStatus::UnsupportedPreRelease, &sink);
    REQUIRE(sink.items().size() == 1);
    CHECK(sink.items()[0].kind == DiagnosticKind::UnsupportedVersion);
    CHECK(sink.items()[0].page_id.empty());
    CHECK(sink.items()[0].detail.find("0.1.4") != std::string::npos);
    CHECK(sink.items()[0].detail.find("0.3.x") != std::string::npos);

    report_version_event(newer, VersionStatus::NewerMajor, &sink);
    REQUIRE(sink.items().size() == 2);
    CHECK(sink.items()[1].kind == DiagnosticKind::MajorVersionMismatch);
    CHECK(sink.items()[1].detail.find("1.0.0") != std::string::npos);
}

TEST_CASE("version: reporting without a sink is allowed") {
    report_version_event({1, 0, 0}, VersionStatus::NewerMajor, nullptr);
    report_version_event({0, 1, 0}, VersionStatus::UnsupportedPreRelease, nullptr);
}

TEST_CASE("version: the host decides about versions that are not supported") {
    CHECK(check_version_policy({0, 3, 0}, VersionStatus::Supported, false).has_value());
    CHECK(check_version_policy({0, 3, 0}, VersionStatus::Supported, true).has_value());

    for (VersionStatus status : {VersionStatus::UnsupportedPreRelease, VersionStatus::NewerMajor}) {
        const Version version = status == VersionStatus::NewerMajor ? Version{1, 0, 0} : Version{0, 1, 0};
        const auto refused = check_version_policy(version, status, false);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::VersionRejected);
        CHECK(refused.error().entry == "manifest.json");
        CHECK(refused.error().message.find(to_string(version)) != std::string::npos);

        CHECK(check_version_policy(version, status, true).has_value());
    }
}

TEST_CASE("version: to_string") {
    CHECK(to_string(Version{0, 2, 0}) == "0.2.0");
    CHECK(to_string(Version{10, 20, 30}) == "10.20.30");
    CHECK(to_string(Version{4294967295u, 0, 1}) == "4294967295.0.1");
}

TEST_CASE("version: the decision can be made before the rest of the manifest is checked") {
    const auto json = parse_json(R"({"onf_version":"1.0.0","title":5,"pages":"a different structure in a newer format"})");
    REQUIRE(json.has_value());

    const auto version = parse_manifest_version(json.value());
    REQUIRE(version.has_value());
    const VersionStatus status = classify_version(version.value());
    CHECK(status == VersionStatus::NewerMajor);

    CollectingSink sink;
    report_version_event(version.value(), status, &sink);
    CHECK(sink.items().size() == 1);
    CHECK(check_version_policy(version.value(), status, false).error().code == ErrorCode::VersionRejected);

    CHECK(parse_manifest(json.value()).error().code == ErrorCode::ManifestInvalidField);
}
