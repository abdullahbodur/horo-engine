#include "Horo/Navigation/NavigationOutcomes.h"

#include "Horo/Navigation/NavigationErrors.h"

namespace Horo::Navigation {
    /** @copydoc MakeNavigationProviderFailure */
    NavigationFailed MakeNavigationProviderFailure(const NavigationProviderFailureCategory category, std::vector<Diagnostic> diagnostics) {
        Error error = MakeError(NavigationErrors::ProviderFailed);
        error.diagnostics = std::move(diagnostics);
        return NavigationFailed{.category = category, .error = std::move(error)};
    }
}  // namespace Horo::Navigation
