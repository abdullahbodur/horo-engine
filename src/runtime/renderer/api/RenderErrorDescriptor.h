#pragma once

#include "Horo/Foundation/ErrorCode.h"

#include <string>
#include <string_view>
#include <utility>

namespace Horo::Render::Detail {
    /** @brief Builds one immutable descriptor in a renderer-owned error domain. */
    inline ErrorCodeDescriptor MakeErrorDescriptor(const ErrorDomainId &domain, std::string code, const ErrorSeverity severity,
                                                   const std::string_view summary, const std::string_view remediation,
                                                   const bool retryable = false) {
        return {domain, ErrorCode{std::move(code)}, severity, summary, remediation, retryable, false};
    }
}  // namespace Horo::Render::Detail
