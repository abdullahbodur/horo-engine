#pragma once

#include "Horo/Navigation/NavigationBackend.h"

#include <span>
#include <vector>

namespace Horo::Navigation::TestSupport {
    /** @brief Deterministic faults exposed only to navigation contract tests. */
    enum class NavigationFixtureFault {
        None,
        Allocation,
        StaleTopology,
        Cancellation
    };

    /** @brief One exact endpoint pair and its byte-stable provider-neutral path. */
    struct NavigationPathFixture final {
        Math::Vec3 start;
        Math::Vec3 destination;
        NavigationPath path;
    };

    /** @brief Small synchronous provider for shared headless contract tests. */
    class DeterministicNavigationQueryBackend final : public INavigationQueryBackend {
    public:
        explicit DeterministicNavigationQueryBackend(std::span<const NavigationPathFixture> fixtures);

        void SetFault(NavigationFixtureFault fault) noexcept;

        [[nodiscard]] NavigationProviderCapabilities Capabilities() const noexcept override;
        [[nodiscard]] Result<NavigationPath> FindPath(const NavigationPathRequest &request,
                                                      const CancellationToken &cancellation) const override;

    private:
        std::vector<NavigationPathFixture> fixtures_;
        NavigationFixtureFault fault_{NavigationFixtureFault::None};
    };
}  // namespace Horo::Navigation::TestSupport
