#pragma once

/**
 * @file SecureRandom.h
 * @brief Native operating-system entropy composition.
 */

#include "Horo/Security/SecureMemory.h"

#include <memory>

namespace Horo::Platform {
    /** @brief Creates the current platform's cryptographically secure OS entropy provider. @return Shared native provider. */
    [[nodiscard]] std::shared_ptr<Security::SecureRandomSource> CreateNativeSecureRandomSource();
}  // namespace Horo::Platform
