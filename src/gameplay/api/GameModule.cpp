#include "Horo/Gameplay/GameModule.h"

#if !defined(HORO_GAMEPLAY_SDK_FINGERPRINT)
#define HORO_GAMEPLAY_SDK_FINGERPRINT "horo-unknown-gameplay-sdk"
#endif

namespace Horo::Gameplay {
    /** @copydoc CurrentGameplayBuildFingerprint */
    std::string_view CurrentGameplayBuildFingerprint() noexcept {
        return HORO_GAMEPLAY_SDK_FINGERPRINT;
    }
}  // namespace Horo::Gameplay
