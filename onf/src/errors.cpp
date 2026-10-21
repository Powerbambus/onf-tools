#include "onf/errors.h"

namespace onf {

namespace {

constexpr ErrorInfo kTable[] = {
#define ONF_X(name, value, text, ref) {ErrorCode::name, text, ref},
    ONF_ERROR_CODES(ONF_X)
#undef ONF_X
};

const ErrorInfo* find(ErrorCode code) noexcept {
    for (const ErrorInfo& info : kTable) {
        if (info.code == code) return &info;
    }
    return nullptr;
}

}  // namespace

std::span<const ErrorInfo> error_table() noexcept { return kTable; }

std::string_view error_name(ErrorCode code) noexcept {
    const ErrorInfo* info = find(code);
    return info != nullptr ? info->name : std::string_view{"unknown"};
}

std::string_view error_spec_ref(ErrorCode code) noexcept {
    const ErrorInfo* info = find(code);
    return info != nullptr ? info->spec_ref : std::string_view{};
}

}  // namespace onf
