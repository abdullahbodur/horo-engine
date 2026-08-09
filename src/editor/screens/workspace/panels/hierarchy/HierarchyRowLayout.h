#pragma once

#include <algorithm>
#include <cstdint>

namespace Horo::Editor {
    /** @brief One horizontal interval in a hierarchy row's local coordinate space. */
    struct HierarchyRowRange {
        float minimum{0.0F};
        float maximum{0.0F};

        /** @brief Returns the non-negative width of this interval. */
        [[nodiscard]] constexpr float Width() const noexcept {
            return maximum > minimum ? maximum - minimum : 0.0F;
        }
    };

    /** @brief DPI-scaled, non-overlapping horizontal layout for one hierarchy row. */
    struct HierarchyRowLayout {
        float height{0.0F};
        HierarchyRowRange row;
        HierarchyRowRange chevron;
        HierarchyRowRange typeIcon;
        HierarchyRowRange label;
        HierarchyRowRange actions;
        HierarchyRowRange visibilityAction;
        HierarchyRowRange lockAction;

        /** @brief Reports whether all row columns are ordered and contained by the row. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return height > 0.0F && row.minimum <= chevron.minimum && chevron.maximum <= typeIcon.minimum &&
                   typeIcon.maximum <= label.minimum && label.maximum <= actions.minimum && actions.minimum == visibilityAction.minimum &&
                   visibilityAction.maximum == lockAction.minimum && lockAction.maximum == actions.maximum &&
                   visibilityAction.Width() == lockAction.Width() && actions.maximum <= row.maximum;
        }
    };

    /**
     * @brief Calculates stable hierarchy columns for the current width, depth, UI scale, and reserved action width.
     * @param availableWidth Current hierarchy content width in logical pixels.
     * @param depth Visible tree depth of the row.
     * @param uiScale Active editor UI scale multiplier.
     * @param actionsWidth Width reserved for persistent or hover action icons.
     * @return Clamped layout whose label shrinks before icon columns overlap.
     */
    [[nodiscard]] inline HierarchyRowLayout CalculateHierarchyRowLayout(const float availableWidth, const std::uint32_t depth,
                                                                        const float uiScale, const float actionsWidth = 0.0F) noexcept {
        const float scale = std::clamp(uiScale, 0.75F, 2.0F);
        const float width = std::max(1.0F, availableWidth);
        const float rightPadding = 6.0F * scale;
        const float indent = (2.0F + static_cast<float>(depth) * 12.0F) * scale;
        // Every row reserves the same chevron column so type icons and labels do
        // not shift when a node gains or loses children. Leaf rows leave it blank.
        const float chevronWidth = 12.0F * scale;
        const float iconWidth = 22.0F * scale;
        // The 16 px icon is left-aligned inside its 22 px slot, leaving 6 px of
        // intrinsic trailing space before this final 1 px label separation.
        const float iconGap = 1.0F * scale;
        const float clampedActionsWidth = std::clamp(actionsWidth, 0.0F, width);

        HierarchyRowLayout layout;
        layout.height = 32.0F * scale;
        layout.row = {0.0F, width};
        const float actionsMaximum = std::max(0.0F, width - rightPadding);
        layout.actions = {std::max(0.0F, actionsMaximum - clampedActionsWidth), actionsMaximum};
        const float actionColumnWidth = layout.actions.Width() * 0.5F;
        layout.visibilityAction = {layout.actions.minimum, layout.actions.minimum + actionColumnWidth};
        layout.lockAction = {layout.visibilityAction.maximum, layout.actions.maximum};
        layout.chevron.minimum = std::min(indent, layout.actions.minimum);
        layout.chevron.maximum = std::min(layout.chevron.minimum + chevronWidth, layout.actions.minimum);
        layout.typeIcon.minimum = layout.chevron.maximum;
        layout.typeIcon.maximum = std::min(layout.typeIcon.minimum + iconWidth, layout.actions.minimum);
        layout.label.minimum = std::min(layout.typeIcon.maximum + iconGap, layout.actions.minimum);
        layout.label.maximum = layout.actions.minimum;
        return layout;
    }
}  // namespace Horo::Editor
