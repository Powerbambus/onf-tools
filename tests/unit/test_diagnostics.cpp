#include "doctest.h"

#include "onf/diagnostics.h"

TEST_CASE("CollectingSink keeps diagnostics in order") {
    onf::CollectingSink sink;
    onf::DiagnosticsSink& channel = sink;

    channel.report({onf::DiagnosticKind::MajorVersionMismatch, "", "file is 1.0.0"});
    channel.report({onf::DiagnosticKind::PageError, "p00002", "bad magic"});

    REQUIRE(sink.items().size() == 2);
    CHECK(sink.items()[0].kind == onf::DiagnosticKind::MajorVersionMismatch);
    CHECK(sink.items()[0].page_id.empty());
    CHECK(sink.items()[1].page_id == "p00002");
}

TEST_CASE("every diagnostic kind has a spec reference") {
    CHECK(onf::diagnostic_spec_ref(onf::DiagnosticKind::MajorVersionMismatch) == "10.3");
    CHECK(onf::diagnostic_spec_ref(onf::DiagnosticKind::UnsupportedVersion) == "10.2");
    CHECK(onf::diagnostic_spec_ref(onf::DiagnosticKind::StrokesVersionMismatch) == "7.2");
    CHECK(onf::diagnostic_spec_ref(onf::DiagnosticKind::PageError) == "12.3.1");
}
