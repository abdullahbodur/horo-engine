/** @file
 * @brief Internal root-policy selection for extension discovery.
 */
#pragma once

#include "Horo/Extensions/ExtensionDiscovery.h"

namespace Horo::Extensions::Discovery {
    /** @brief Canonical approved discovery boundary, not a verified package or activation grant. */
    struct ApprovedRoot {
        std::string id;
        std::filesystem::path canonicalPath;
        RootKind kind;
    };

    /** @brief Deterministic approved roots and diagnostics sorted by stable root ID. */
    struct RootSelection {
        std::vector<ApprovedRoot> roots;
        std::vector<RootDiagnostic> diagnostics;
        bool nonPortable{false}; /**< True only when a development override is accepted. */
    };

    /**
     * @brief Apply explicit approval and local-only override policy before filesystem access.
     * @param requests At most 64 roots supplied by trusted product/package composition.
     * @param policy Invocation profile and explicit override opt-in.
     * @return Canonical approved boundaries, or a typed error for invalid IDs or accepted paths.
     * @note Does not read project configuration, environment variables or extension manifests;
     *       callers must preserve configuration provenance and actual local approval decisions.
     */
    [[nodiscard]] Result<RootSelection> SelectApprovedRoots(std::span<const RootRequest> requests, const RootPolicy &policy);
}  // namespace Horo::Extensions::Discovery
