#include "Horo/Navigation/Backends/NullProvider.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <limits>
#include <new>

namespace Horo::Navigation {
    namespace {
        [[nodiscard]] NavigationProviderCapabilities NullCapabilities() noexcept {
            constexpr NavigationQueryLimits limits{
                .maximumNodeExpansions = std::numeric_limits<std::uint32_t>::max(),
                .maximumResultPoints = std::numeric_limits<std::uint32_t>::max(),
                .maximumSearchDistanceMeters = std::numeric_limits<float>::max(),
            };
            return MakeAvailablePathQueryCapabilities(1, limits, 1);
        }

        class NullNavigationQueryBackend final : public INavigationQueryBackend {
        public:
            /** @copydoc INavigationQueryBackend::Capabilities */
            [[nodiscard]] NavigationProviderCapabilities Capabilities() const noexcept override {
                static const NavigationProviderCapabilities capabilities = NullCapabilities();
                return capabilities;
            }

            /** @copydoc INavigationQueryBackend::FindPath */
            [[nodiscard]] Result<NavigationPath> FindPath(const NavigationPathRequest &, const CancellationToken &) const override {
                return Result<NavigationPath>::Failure(MakeError(NavigationErrors::NoNavigationData));
            }
        };
    }  // namespace

    /** @copydoc CreateNullNavigationQueryBackend */
    Result<std::unique_ptr<INavigationQueryBackend>> CreateNullNavigationQueryBackend() {
        auto provider = std::unique_ptr<INavigationQueryBackend>{new (std::nothrow) NullNavigationQueryBackend{}};
        if (!provider)
            return Result<std::unique_ptr<INavigationQueryBackend>>::Failure(MakeError(NavigationErrors::CapacityExceeded));
        return Result<std::unique_ptr<INavigationQueryBackend>>::Success(std::move(provider));
    }
}  // namespace Horo::Navigation
