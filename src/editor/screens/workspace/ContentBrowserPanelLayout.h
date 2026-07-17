#pragma once

#include "Horo/Editor/EditorTheme.h"

namespace Horo::Editor
{
    /** @brief Smallest readable text size in the global dock, matching its tab labels. */
    inline constexpr float kGlobalDockMinimumFontSize = Theme::FontPx::SansCompact;

    /** @brief Responsive grid metrics matching the workspace HTML asset-grid contract. */
    struct ContentBrowserGridMetrics
    {
        std::size_t columns{1};
        float cardWidth{1.0F};
    };

    /**
     * @brief Computes CSS-like `auto-fill minmax(66px, 1fr)` asset-card geometry.
     * @param availableWidth Width available to the grid after outer padding.
     * @return Column count and equal card width using the canonical six-pixel gap.
     */
    [[nodiscard]] ContentBrowserGridMetrics ComputeContentBrowserGridMetrics(float availableWidth) noexcept;
} // namespace Horo::Editor
