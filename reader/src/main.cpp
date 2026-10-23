#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "info.h"
#include "onf/container/bytesource.h"
#include "onf/read/document.h"
#include "svg_export.h"

namespace {

using namespace onf;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 1;
constexpr int kExitFileError = 2;
constexpr int kExitPageError = 3;
constexpr int kExitRejected = 4;

constexpr const char* kUsage =
    "usage:\n"
    "  onf-reader info FILE [options]\n"
    "  onf-reader svg  FILE [--page ID | --number N] [-o OUT.svg] [options]\n"
    "\n"
    "options:\n"
    "  --allow-unsupported-version   open a file whose ONF version this reader does not implement\n"
    "\n"
    "svg exports the page with that id (p00001) or display number (default: the first displayed page).\n"
    "Events (missing assets, version mismatches, page errors) go to stderr.\n"
    "exit codes: 0 ok, 1 usage, 2 the file cannot be read, 3 a page cannot be read, 4 version rejected\n";

const char* kind_name(DiagnosticKind kind) {
    switch (kind) {
        case DiagnosticKind::MajorVersionMismatch:   return "major_version_mismatch";
        case DiagnosticKind::UnsupportedVersion:     return "unsupported_version";
        case DiagnosticKind::StrokesVersionMismatch: return "strokes_version_mismatch";
        case DiagnosticKind::PageError:              return "page_error";
        case DiagnosticKind::MissingAsset:           return "missing_asset";
        case DiagnosticKind::CorruptAsset:           return "corrupt_asset";
        case DiagnosticKind::UnsupportedAssetFormat: return "unsupported_asset_format";
    }
    return "unknown";
}

// Events are for the host application to decide on; the command line only shows them.
class StderrSink final : public DiagnosticsSink {
public:
    void report(const Diagnostic& d) override {
        std::cerr << "event " << kind_name(d.kind) << " (spec " << diagnostic_spec_ref(d.kind) << ")";
        if (!d.page_id.empty()) std::cerr << " page " << d.page_id;
        std::cerr << ": " << d.detail << "\n";
    }
};

struct Arguments {
    std::string command;
    std::string file;
    std::string page_id;
    int page_number = 0;
    std::string output;
    bool allow_unsupported = false;
};

bool parse_arguments(int argc, char** argv, Arguments& args) {
    std::vector<std::string_view> words(argv + 1, argv + argc);
    if (words.size() < 2) return false;
    args.command = words[0];
    args.file = words[1];
    for (std::size_t i = 2; i < words.size(); ++i) {
        const std::string_view word = words[i];
        const auto value = [&]() -> std::string_view { return i + 1 < words.size() ? words[++i] : std::string_view{}; };
        if (word == "--allow-unsupported-version") {
            args.allow_unsupported = true;
        } else if (word == "--page") {
            args.page_id = value();
            if (args.page_id.empty()) return false;
        } else if (word == "--number") {
            const std::string text(value());
            try {
                std::size_t used = 0;
                args.page_number = std::stoi(text, &used);
                if (used != text.size() || args.page_number < 1) return false;
            } catch (const std::exception&) {
                return false;
            }
        } else if (word == "-o") {
            args.output = value();
            if (args.output.empty()) return false;
        } else {
            return false;
        }
    }
    return (args.command == "info" || args.command == "svg") && !(!args.page_id.empty() && args.page_number != 0);
}

}  // namespace

int main(int argc, char** argv) {
    Arguments args;
    if (!parse_arguments(argc, argv, args)) {
        std::cerr << kUsage;
        return kExitUsage;
    }

    StderrSink sink;

    auto source = ByteSource::from_file(args.file);
    if (!source) {
        std::cerr << "error: " << onf_reader::describe_error(source.error().code, source.error().message) << "\n";
        return kExitFileError;
    }

    // Opening is two steps (spec 10.2, 10.3): inspect shows the version, the host then decides.
    auto inspection = inspect(std::move(source).value(), &sink);
    if (!inspection) {
        std::cerr << "error: " << onf_reader::describe_error(inspection.error().code, inspection.error().message) << "\n";
        return kExitFileError;
    }
    OpenDecision decision;
    decision.allow_unsupported_version = args.allow_unsupported;
    auto opened = std::move(inspection).value().open(decision);
    if (!opened) {
        std::cerr << "error: " << onf_reader::describe_error(opened.error().code, opened.error().message) << "\n";
        return opened.error().code == ErrorCode::VersionRejected ? kExitRejected : kExitFileError;
    }
    const Document& document = opened.value();

    if (args.command == "info") {
        return onf_reader::print_info(document, std::cout, &sink) == 0 ? kExitOk : kExitPageError;
    }

    // svg
    std::string id = args.page_id;
    if (id.empty()) {
        const auto& pages = document.pages();
        const std::size_t index = args.page_number == 0 ? 0 : static_cast<std::size_t>(args.page_number - 1);
        if (index >= pages.size()) {
            std::cerr << "error: the document has " << pages.size() << " pages\n";
            return kExitUsage;
        }
        id = pages[index].id;
    }
    auto page = document.page(id, &sink);
    if (!page) {
        std::cerr << "error: page " << id << ": " << onf_reader::describe_error(page.error().code, page.error().message) << "\n";
        return kExitPageError;
    }

    const std::string svg = onf_reader::export_page_svg(document, page.value(), &sink);
    if (args.output.empty()) {
        std::cout << svg;
    } else {
        std::ofstream out(args.output, std::ios::binary);
        out << svg;
        if (!out) {
            std::cerr << "error: cannot write " << args.output << "\n";
            return kExitFileError;
        }
    }
    return kExitOk;
}
