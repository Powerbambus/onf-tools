#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/read/document.h"
#include "onf/write/writer.h"
#include "rewrite.h"

namespace {

using namespace onf;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 1;
constexpr int kExitReadError = 2;
constexpr int kExitNotCopyable = 3;
constexpr int kExitRejected = 4;
constexpr int kExitWriteError = 5;

constexpr const char* kUsage =
    "usage:\n"
    "  onf-writer rewrite IN.onf OUT.onf [--allow-unsupported-version]\n"
    "\n"
    "rewrite reads a file completely and writes it again as ONF 0.4.0, with the pages renumbered and the\n"
    "generator replaced. It stops without writing if a page or an asset cannot be copied faithfully.\n"
    "Events go to stderr.\n"
    "exit codes: 0 ok, 1 usage, 2 the input cannot be read, 3 something cannot be copied, 4 version rejected,\n"
    "5 the output cannot be written\n";

class StderrSink final : public DiagnosticsSink {
public:
    void report(const Diagnostic& d) override {
        std::cerr << "event (spec " << diagnostic_spec_ref(d.kind) << ")";
        if (!d.page_id.empty()) std::cerr << " page " << d.page_id;
        std::cerr << ": " << d.detail << "\n";
    }
};

std::string describe(ErrorCode code, const std::string& message) {
    return std::to_string(static_cast<unsigned>(code)) + " " + std::string(error_name(code)) + " (spec " + std::string(error_spec_ref(code)) +
           "): " + message;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> words(argv + 1, argv + argc);
    bool allow_unsupported = false;
    std::vector<std::string_view> positional;
    for (const std::string_view word : words) {
        if (word == "--allow-unsupported-version") allow_unsupported = true;
        else positional.push_back(word);
    }
    if (positional.size() != 3 || positional[0] != "rewrite") {
        std::cerr << kUsage;
        return kExitUsage;
    }
    const std::string input(positional[1]);
    const std::string output(positional[2]);
    if (input == output) {
        std::cerr << "error: the output must not be the input file\n";
        return kExitUsage;
    }

    StderrSink sink;
    auto source = ByteSource::from_file(input);
    if (!source) {
        std::cerr << "error: " << describe(source.error().code, source.error().message) << "\n";
        return kExitReadError;
    }
    auto inspection = inspect(std::move(source).value(), &sink);
    if (!inspection) {
        std::cerr << "error: " << describe(inspection.error().code, inspection.error().message) << "\n";
        return kExitReadError;
    }
    OpenDecision decision;
    decision.allow_unsupported_version = allow_unsupported;
    auto opened = std::move(inspection).value().open(decision);
    if (!opened) {
        std::cerr << "error: " << describe(opened.error().code, opened.error().message) << "\n";
        return opened.error().code == ErrorCode::VersionRejected ? kExitRejected : kExitReadError;
    }

    std::vector<std::string> notes;
    auto collected = onf_writer::collect_document(opened.value(), &sink, notes);
    if (!collected) {
        std::cerr << "error: " << describe(collected.error().code, collected.error().message) << "\n";
        return kExitNotCopyable;
    }

    for (const std::string& note : notes) std::cerr << "note: " << note << "\n";

    auto written = write_onf(collected.value(), output);
    if (!written) {
        std::cerr << "error: " << describe(written.error().code, written.error().message) << "\n";
        return written.error().code == ErrorCode::WriteInvalidDocument ? kExitNotCopyable : kExitWriteError;
    }
    return kExitOk;
}
