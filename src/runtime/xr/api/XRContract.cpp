#include "Horo/XR/XRContract.h"

#include "Horo/XR/XRErrors.h"

namespace Horo::XR {
    /** @copydoc RequireXRContractVersion */
    Result<void> RequireXRContractVersion(const XRContractVersion required, const XRContractVersion provided) {
        if (!required.IsValid() || !provided.IsValid())
            return Result<void>::Failure(MakeError(XRErrors::ContractVersionInvalid));
        if (required.major != provided.major || provided.minor < required.minor)
            return Result<void>::Failure(MakeError(XRErrors::ContractVersionIncompatible));
        return Result<void>::Success();
    }
}  // namespace Horo::XR
