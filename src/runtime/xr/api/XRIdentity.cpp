#include "Horo/XR/XRIdentity.h"

#include "Horo/XR/XRErrors.h"

namespace Horo::XR {
    /** @copydoc ValidateXRSystem */
    Result<void> ValidateXRSystem(const XRSystemId &system, const XRSystemId &activeSystem) {
        if (!system.IsValid() || !activeSystem.IsValid())
            return Result<void>::Failure(MakeError(XRErrors::IdentityInvalid));
        if (system != activeSystem)
            return Result<void>::Failure(MakeError(XRErrors::IdentityStale));
        return Result<void>::Success();
    }

    /** @copydoc ValidateXRSession */
    Result<void> ValidateXRSession(const XRSessionId &session, const XRSessionId &activeSession) {
        if (!session.IsValid() || !activeSession.IsValid())
            return Result<void>::Failure(MakeError(XRErrors::IdentityInvalid));
        if (session != activeSession)
            return Result<void>::Failure(MakeError(XRErrors::IdentityStale));
        return Result<void>::Success();
    }
}  // namespace Horo::XR
