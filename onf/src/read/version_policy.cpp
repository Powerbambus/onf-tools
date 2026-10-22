#include "onf/read/version_policy.h"

#include <string>

namespace onf {

VersionStatus classify_version(const Version& version) noexcept {
    if (version.major > kImplementedVersion.major) return VersionStatus::NewerMajor;
    if (version.minor != kImplementedVersion.minor) return VersionStatus::UnsupportedPreRelease;
    return VersionStatus::Supported;
}

void report_version_event(const Version& version, VersionStatus status, DiagnosticsSink* sink) {
    if (sink == nullptr || status == VersionStatus::Supported) return;

    const std::string detail = "file version " + to_string(version) + ", this reader implements " +
                               std::to_string(kImplementedVersion.major) + "." + std::to_string(kImplementedVersion.minor) + ".x";
    const DiagnosticKind kind = status == VersionStatus::NewerMajor ? DiagnosticKind::MajorVersionMismatch
                                                                    : DiagnosticKind::UnsupportedVersion;
    sink->report({kind, {}, detail});
}

Result<void, FileError> check_version_policy(const Version& version, VersionStatus status, bool host_allows) {
    if (status == VersionStatus::Supported || host_allows) return {};
    return unexpected(FileError{ErrorCode::VersionRejected,
                                "version " + to_string(version) + " is not supported and the host did not allow opening it",
                                "manifest.json"});
}

}  // namespace onf
