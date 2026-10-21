#pragma once

// Events the host application decides on. The caller passes in a DiagnosticsSink.

#include <string>
#include <string_view>
#include <vector>

namespace onf {

enum class DiagnosticKind {
    MajorVersionMismatch,
    UnsupportedVersion,
    StrokesVersionMismatch,
    PageError,
};

constexpr std::string_view diagnostic_spec_ref(DiagnosticKind kind) noexcept {
    switch (kind) {
        case DiagnosticKind::MajorVersionMismatch:   return "10.3";
        case DiagnosticKind::UnsupportedVersion:     return "10.2";
        case DiagnosticKind::StrokesVersionMismatch: return "7.2";
        case DiagnosticKind::PageError:              return "12.3.1";
    }
    return {};
}

struct Diagnostic {
    DiagnosticKind kind;
    std::string page_id;  // empty for document-level events
    std::string detail;
};

class DiagnosticsSink {
public:
    virtual ~DiagnosticsSink() = default;
    virtual void report(const Diagnostic& diagnostic) = 0;
};

class CollectingSink final : public DiagnosticsSink {
public:
    void report(const Diagnostic& diagnostic) override { items_.push_back(diagnostic); }
    const std::vector<Diagnostic>& items() const noexcept { return items_; }

private:
    std::vector<Diagnostic> items_;
};

}  // namespace onf
