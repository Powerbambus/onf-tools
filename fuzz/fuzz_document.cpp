// Feeds arbitrary bytes to the whole read path: ZIP container, manifest, every page, and every asset.
// Whatever the input is, the library must return an error or a result, never crash or run away.
#include <cstdint>
#include <vector>

#include "onf/container/bytesource.h"
#include "onf/read/document.h"

namespace {

class DiscardSink final : public onf::DiagnosticsSink {
public:
    void report(const onf::Diagnostic&) override {}
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    DiscardSink sink;
    auto inspection = onf::inspect(onf::ByteSource::from_memory(std::vector<std::uint8_t>(data, data + size)), &sink);
    if (!inspection) return 0;

    onf::OpenDecision decision;
    decision.allow_unsupported_version = true;  // go on to the parts the fuzzer is really after
    auto document = std::move(inspection).value().open(decision);
    if (!document) return 0;

    static_cast<void>(document.value().thumbnail_bytes());
    for (const onf::PageEntry& entry : document.value().pages()) {
        auto page = document.value().page(entry.id, &sink);
        if (!page) continue;
        for (const onf::AssetRef& asset : page.value().meta.assets) static_cast<void>(document.value().asset(asset, entry.id, &sink));
    }
    return 0;
}
