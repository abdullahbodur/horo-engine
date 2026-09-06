#pragma once

/**
 * @file NavigationBackend.h
 * @brief Provider-neutral grounded navigation query execution contract.
 */

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/Navigation/NavigationCapabilities.h"
#include "Horo/Navigation/NavigationIdentity.h"

#include <vector>

namespace Horo::Navigation {
    /** @brief Bounded provider-neutral request for one grounded path. */
    struct NavigationPathRequest final {
        NavigationWorldId world;                /**< Exact active world captured at admission. */
        NavigationGeneration topology;          /**< Exact immutable topology captured at admission. */
        Math::Vec3 start;                       /**< Start in Horo right-handed, Y-up world space. */
        Math::Vec3 destination;                 /**< Destination in the same origin revision as start. */
        NavigationQueryRequirement requirement; /**< Exact admitted quality and execution bounds. */
    };

    /** @brief Provider-neutral ordered path points with no native polygon or node identity. */
    struct NavigationPath final {
        std::vector<Math::Vec3> points; /**< Ordered world-space points, including declared endpoints. */
        float lengthMeters{};           /**< Finite non-negative path length in metres. */
    };

    /**
     * @brief Synchronous provider execution seam invoked only from NavigationRuntime-owned work.
     *
     * This interface owns no scheduling, callbacks, world lifetime, or completion publication.
     * The runtime validates capabilities and bounds before dispatch, supplies an immutable
     * topology generation, and translates returned typed errors into terminal outcomes.
     */
    class INavigationQueryBackend {
    public:
        virtual ~INavigationQueryBackend() = default;

        /** @brief Returns immutable composition-time capability evidence by value. */
        [[nodiscard]] virtual NavigationProviderCapabilities Capabilities() const noexcept = 0;

        /**
         * @brief Executes one already-admitted grounded path query.
         * @param request Owned provider-neutral request for an exact world and topology.
         * @param cancellation Cooperative cancellation observed during provider work.
         * @return Path, or a typed Horo navigation error without provider-native values.
         */
        [[nodiscard]] virtual Result<NavigationPath> FindPath(const NavigationPathRequest &request,
                                                              const CancellationToken &cancellation) const = 0;
    };
}  // namespace Horo::Navigation
