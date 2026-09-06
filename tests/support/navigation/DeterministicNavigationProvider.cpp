#include "navigation/DeterministicNavigationProvider.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <limits>

namespace Horo::Navigation::TestSupport {
    namespace {
        [[nodiscard]] NavigationProviderCapabilities FixtureCapabilities() noexcept {
            constexpr NavigationQueryLimits limits{
                .maximumNodeExpansions = std::numeric_limits<std::uint32_t>::max(),
                .maximumResultPoints = std::numeric_limits<std::uint32_t>::max(),
                .maximumSearchDistanceMeters = std::numeric_limits<float>::max(),
            };
            return MakeAvailablePathQueryCapabilities(1, limits, 1);
        }
    }  // namespace

    DeterministicNavigationQueryBackend::DeterministicNavigationQueryBackend(const std::span<const NavigationPathFixture> fixtures)
        : fixtures_(fixtures.begin(), fixtures.end()) {}

    void DeterministicNavigationQueryBackend::SetFault(const NavigationFixtureFault fault) noexcept {
        fault_.store(fault, std::memory_order_relaxed);
    }

    NavigationProviderCapabilities DeterministicNavigationQueryBackend::Capabilities() const noexcept {
        return FixtureCapabilities();
    }

    Result<NavigationPath> DeterministicNavigationQueryBackend::FindPath(const NavigationPathRequest &request,
                                                                         const CancellationToken &cancellation) const {
        const NavigationFixtureFault fault = fault_.load(std::memory_order_relaxed);
        if (fault == NavigationFixtureFault::Cancellation || cancellation.IsCancellationRequested())
            return Result<NavigationPath>::Failure(MakeError(NavigationErrors::QueryCancelled));
        if (fault == NavigationFixtureFault::Allocation)
            return Result<NavigationPath>::Failure(MakeError(NavigationErrors::CapacityExceeded));
        if (fault == NavigationFixtureFault::StaleTopology)
            return Result<NavigationPath>::Failure(MakeError(NavigationErrors::StaleSnapshot));

        for (const NavigationPathFixture &fixture : fixtures_) {
            if (fixture.start == request.start && fixture.destination == request.destination)
                return Result<NavigationPath>::Success(fixture.path);
        }
        return Result<NavigationPath>::Failure(MakeError(NavigationErrors::NoNavigationData));
    }
}  // namespace Horo::Navigation::TestSupport
