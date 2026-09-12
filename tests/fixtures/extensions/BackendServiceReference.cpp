#include "BackendServiceReference.h"

#include "Horo/Extensions/ExtensionErrors.h"

namespace Horo::Extensions::Fixtures {
    ArithmeticService::ArithmeticService(std::shared_ptr<ArithmeticServiceAudit> audit) noexcept : audit_(std::move(audit)) {}

    /** @copydoc ArithmeticService::Add */
    Result<SumResponse> ArithmeticService::Add(const SumRequest &request, const BackendServiceCallContext &context) {
        audit_->observedProvider = context.Provider().provider.providerId;
        return Result<SumResponse>::Success({request.left + request.right});
    }

    /** @copydoc ArithmeticService::Fail */
    Result<SumResponse> ArithmeticService::Fail(const SumRequest &, const BackendServiceCallContext &) const {
        return Result<SumResponse>::Failure(MakeError(ExtensionErrors::ContributionRejected, "reference failure"));
    }

    /** @copydoc ArithmeticService::Shutdown */
    void ArithmeticService::Shutdown() noexcept {
        ++audit_->shutdownCount;
    }
}  // namespace Horo::Extensions::Fixtures
