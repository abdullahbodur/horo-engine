#include "navigation/DeterministicNavigationProvider.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <limits>

namespace Horo::Navigation::TestSupport {
    namespace {
        [[nodiscard]] NavigationProviderCapabilities FixtureCapabilities() noexcept {
            NavigationProviderCapabilities capabilities;
            capabilities.revision = 1;
            capabilities.availability = NavigationProviderAvailability::Available;
            capabilities.capabilities.fill(NavigationSupport::Unsupported);
            capabilities.capabilities[static_cast<std::size_t>(NavigationCapability::GroundedQueries)] = NavigationSupport::Available;
            capabilities.querySupport[static_cast<std::size_t>(NavigationQueryKind::Path)].fill(NavigationSupport::Available);
            capabilities.queryLimits[static_cast<std::size_t>(NavigationQueryKind::Path)].fill({
                .maximumNodeExpansions = std::numeric_limits<std::uint32_t>::max(),
                .maximumResultPoints = std::numeric_limits<std::uint32_t>::max(),
                .maximumSearchDistanceMeters = std::numeric_limits<float>::max(),
            });
            capabilities.maximumConcurrentQueries = 1;
            return capabilities;
        }
    }  // namespace

    DeterministicNavigationQueryBackend::DeterministicNavigationQueryBackend(const std::span<const NavigationPathFixture> fixtures)
        : fixtures_(fixtures.begin(), fixtures.end()) {}

    void DeterministicNavigationQueryBackend::SetFault(const NavigationFixtureFault fault) noexcept {
        fault_ = fault;
    }

    NavigationProviderCapabilities DeterministicNavigationQueryBackend::Capabilities() const noexcept {
        return FixtureCapabilities();
    }

    Result<NavigationPath> DeterministicNavigationQueryBackend::FindPath(const NavigationPathRequest &request,
                                                                         const CancellationToken &cancellation) const {
        if (fault_ == NavigationFixtureFault::Cancellation || cancellation.IsCancellationRequested())
            return Result<NavigationPath>::Failure(MakeError(NavigationErrors::QueryCancelled));
        if (fault_ == NavigationFixtureFault::Allocation)
            return Result<NavigationPath>::Failure(MakeError(NavigationErrors::CapacityExceeded));
        if (fault_ == NavigationFixtureFault::StaleTopology)
            return Result<NavigationPath>::Failure(MakeError(NavigationErrors::StaleSnapshot));

        for (const NavigationPathFixture &fixture : fixtures_) {
            if (fixture.start == request.start && fixture.destination == request.destination)
                return Result<NavigationPath>::Success(fixture.path);
        }
        return Result<NavigationPath>::Failure(MakeError(NavigationErrors::NoNavigationData));
    }
}  // namespace Horo::Navigation::TestSupport
