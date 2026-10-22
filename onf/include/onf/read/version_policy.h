#pragma once

#include "onf/diagnostics.h"
#include "onf/errors.h"
#include "onf/model/version.h"
#include "onf/result.h"

namespace onf {

// This reader implements ONF 0.2.x.
inline constexpr Version kImplementedVersion{0, 2, 0};

enum class VersionStatus {
    Supported,              // 0.2.x
    UnsupportedPreRelease,  // any other 0.x.y: pre-release versions may break at every release (spec 10.1)
    NewerMajor,             // MAJOR 1 or higher (spec 10.3)
};

VersionStatus classify_version(const Version& version) noexcept;

// Reports UnsupportedVersion (spec 10.2) or MajorVersionMismatch (spec 10.3) to the sink. Nothing for Supported.
void report_version_event(const Version& version, VersionStatus status, DiagnosticsSink* sink);

// A version that is not supported is only opened if the host has allowed it. Error: VersionRejected.
Result<void, FileError> check_version_policy(const Version& version, VersionStatus status, bool host_allows);

}  // namespace onf
