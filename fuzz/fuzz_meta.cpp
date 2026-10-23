// meta.json on its own: the strict JSON parser and the rules of spec 6.2, 8 and 9.3.
#include <cstdint>
#include <span>

#include "onf/read/json.h"
#include "onf/read/manifest.h"
#include "onf/read/page_meta.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    if (auto json = onf::parse_json(bytes, onf::JsonOptions{true})) {
        static_cast<void>(onf::parse_page_meta(json.value(), "p00001"));
    }
    if (auto json = onf::parse_json(bytes)) static_cast<void>(onf::parse_manifest(json.value()));
    return 0;
}
