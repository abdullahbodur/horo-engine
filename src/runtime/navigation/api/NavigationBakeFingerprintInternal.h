#pragma once

#include "Horo/Navigation/NavigationBakeInput.h"

#include <span>

namespace Horo::Navigation::Internal {
    /** @brief Computes the deterministic digest for canonical tile-build semantics and source provenance. */
    [[nodiscard]] Sha256Digest ComputeBakeInputFingerprint(const NavigationBakeInputRevisions &revisions,
                                                           std::span<const NavigationResolvedBakeProfile> profiles,
                                                           std::span<const NavigationResolvedBakeArea> areas,
                                                           std::span<const NavigationTileBuildPartition> partitions,
                                                           std::span<const NavigationTileBuildTriangle> triangles,
                                                           std::span<const NavigationTileBuildModifier> modifiers) noexcept;
}  // namespace Horo::Navigation::Internal
