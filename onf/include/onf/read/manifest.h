#pragma once

#include "onf/container/zip_archive.h"
#include "onf/errors.h"
#include "onf/model/manifest.h"
#include "onf/read/json.h"
#include "onf/result.h"

namespace onf {

// manifest.json as parsed JSON. Errors: ManifestMissing, ManifestTooLarge, read errors, JSON errors.
Result<JsonValue, FileError> read_manifest_json(const ZipArchive& archive);

// Only the version, so that the version policy can decide before anything else is checked.
Result<Version, FileError> parse_manifest_version(const JsonValue& root);

// All rules of spec 6.1.2. Unknown keys are ignored.
Result<Manifest, FileError> parse_manifest(const JsonValue& root);

// Every page of the manifest needs a pages/pNNNNN/ directory in the archive (an entry below it is enough).
Result<void, FileError> check_page_directories(const ZipArchive& archive, const Manifest& manifest);

// read_manifest_json, parse_manifest and check_page_directories. The version policy is not applied here.
Result<Manifest, FileError> read_manifest(const ZipArchive& archive);

}  // namespace onf
