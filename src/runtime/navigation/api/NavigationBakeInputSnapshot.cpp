#include "Horo/Navigation/NavigationBakeInput.h"

namespace Horo::Navigation {
    /** @copydoc NavigationBakeInputSnapshot::Revisions */
    const NavigationBakeInputRevisions &NavigationBakeInputSnapshot::Revisions() const noexcept {
        return revisions_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Limits */
    const NavigationBakeInputLimits &NavigationBakeInputSnapshot::Limits() const noexcept {
        return limits_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Fingerprint */
    const Sha256Digest &NavigationBakeInputSnapshot::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Profiles */
    std::span<const NavigationResolvedBakeProfile> NavigationBakeInputSnapshot::Profiles() const noexcept {
        return profiles_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Areas */
    std::span<const NavigationResolvedBakeArea> NavigationBakeInputSnapshot::Areas() const noexcept {
        return areas_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Partitions */
    std::span<const NavigationTileBuildPartition> NavigationBakeInputSnapshot::Partitions() const noexcept {
        return partitions_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Triangles */
    std::span<const NavigationTileBuildTriangle> NavigationBakeInputSnapshot::Triangles() const noexcept {
        return triangles_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Modifiers */
    std::span<const NavigationTileBuildModifier> NavigationBakeInputSnapshot::Modifiers() const noexcept {
        return modifiers_;
    }
}  // namespace Horo::Navigation
