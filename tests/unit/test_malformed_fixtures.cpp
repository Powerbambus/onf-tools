#include "doctest.h"

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/read/document.h"

using namespace onf;

#ifndef ONF_FIXTURES_DIR
#error "ONF_FIXTURES_DIR must point to tests/fixtures"
#endif

namespace {

struct Expected {
    std::string file;
    std::string scope;  // "file" or "page"
    std::string error;
    std::string section;
};

std::vector<Expected> load_expected() {
    const std::string path = std::string(ONF_FIXTURES_DIR) + "/malformed/expected.tsv";
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.is_open(), "missing " << path << " (run tests/fixtures/make_malformed_onf.py)");
    std::vector<Expected> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream cells(line);
        Expected row;
        std::getline(cells, row.file, '\t');
        std::getline(cells, row.scope, '\t');
        std::getline(cells, row.error, '\t');
        std::getline(cells, row.section, '\t');
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace

// One broken file per case of spec 12.3, written by tests/fixtures/make_malformed_onf.py. A file-level error rejects
// the whole file; a page-level error makes page p00001 unreadable while p00002 stays readable (spec 12.3.1).
TEST_CASE("malformed fixtures: every file fails in the scope and with the error the table says") {
    const std::vector<Expected> expected = load_expected();
    REQUIRE(expected.size() > 50);

    std::set<std::string> seen_errors;
    for (const Expected& row : expected) {
        CAPTURE(row.file);
        const std::string path = std::string(ONF_FIXTURES_DIR) + "/malformed/" + row.file + ".onf";

        CollectingSink sink;
        std::string file_error;
        std::optional<Document> document;

        auto source = ByteSource::from_file(path);
        REQUIRE_MESSAGE(source.has_value(), "cannot read " << path);
        auto inspection = inspect(std::move(source).value(), &sink);
        if (!inspection) {
            file_error = std::string(error_name(inspection.error().code));
        } else {
            auto opened = std::move(inspection).value().open();
            if (!opened) {
                file_error = std::string(error_name(opened.error().code));
            } else {
                document.emplace(std::move(opened).value());
            }
        }

        if (row.scope == "file") {
            CHECK_FALSE(document.has_value());
            CHECK(file_error == row.error);
        } else {
            REQUIRE_MESSAGE(document.has_value(), "the file was rejected as a whole: " << file_error);
            const auto bad = document->page("p00001", &sink);
            REQUIRE_FALSE(bad.has_value());
            CHECK(std::string(error_name(bad.error().code)) == row.error);
            CHECK(bad.error().page_id == "p00001");

            // The other page is untouched.
            const auto good = document->page("p00002");
            CHECK_MESSAGE(good.has_value(), "p00002 should stay readable");
        }
        seen_errors.insert(row.error);
    }

    // The table covers many different errors, not one case over and over.
    CHECK(seen_errors.size() > 30);
}

TEST_CASE("malformed fixtures: every error code of the table names a real error") {
    std::set<std::string> known;
    for (const ErrorInfo& info : error_table()) known.insert(std::string(info.name));
    for (const Expected& row : load_expected()) {
        CAPTURE(row.file);
        CHECK(known.contains(row.error));
    }
}
