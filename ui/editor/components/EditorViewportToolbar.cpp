/** @file EditorViewportToolbar.cpp
 *  @brief Implements viewport title-bar tool controls. */
#include "ui/editor/components/EditorViewportToolbar.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ui/IconsFontAwesome6.h"
#include "ui/UiComponents.h"

namespace Horo::Editor {
    namespace {
        /** @brief One supported transform precision step. */
        struct TranslatePrecisionOption {
            const char *label = "";
            float meters = 0.0f;
            float rotationDegrees = 0.0f;
        };

        constexpr std::array<TranslatePrecisionOption, 5>
                kTranslatePrecisionOptions = {
                    {
                        {"1 cm", 0.01f, 1.0f},
                        {"5 cm", 0.05f, 5.0f},
                        {"10 cm", 0.10f, 10.0f},
                        {"50 cm", 0.50f, 45.0f},
                        {"1 m", 1.0f, 90.0f},
                    }
                };

        /** @brief Returns the supported precision option closest to @p stepMeters. */
        int TranslatePrecisionIndex(float stepMeters) {
            int bestIndex = 0;
            float bestDistance = std::abs(
                    stepMeters - kTranslatePrecisionOptions.front().meters);
            for (int i = 1;
                 i < static_cast<int>(kTranslatePrecisionOptions.size()); ++i) {
                const float distance =
                        std::abs(stepMeters -
                                 kTranslatePrecisionOptions[static_cast<size_t>(i)]
                                         .meters);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestIndex = i;
                }
            }
            return bestIndex;
        }

        /** @brief Renders the title bar frame shared by viewport toolbar tools. */
        void DrawViewportToolbarFrame(const Ui::EditorTheme &theme,
                                      const char *title, const ImVec2 cursor,
                                      float contentMinX, float contentMaxX,
                                      float height) {
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(ImVec2(contentMinX, cursor.y),
                                    ImVec2(contentMaxX, cursor.y + height),
                                    ImGui::GetColorU32(theme.palette.panel));
            drawList->AddRectFilled(ImVec2(contentMinX, cursor.y + height - 1.0f),
                                    ImVec2(contentMaxX, cursor.y + height),
                                    ImGui::GetColorU32(theme.palette.border));

            constexpr float kHorizontalPadding = 14.0f;
            constexpr float kVerticalPadding = 8.0f;
            ImGui::SetCursorScreenPos(ImVec2(contentMinX + kHorizontalPadding,
                                             cursor.y + kVerticalPadding));
            ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
            ImGui::TextUnformatted(title ? title : "");
            ImGui::PopStyleColor();
        }

        /** @brief Renders the compact precision dropdown in the viewport title bar. */
        void DrawPrecisionTool(const Ui::EditorTheme &theme,
                               const EditorViewportToolbarState &state,
                               const EditorViewportToolbarCallbacks &callbacks,
                               const ImVec2 cursor, float contentMaxX,
                               float height) {
            constexpr float kHorizontalPadding = 14.0f;
            constexpr float kToolGap = 8.0f;
            constexpr float kComboWidth = 82.0f;
            const float iconWidth = ImGui::CalcTextSize(ICON_FA_SLIDERS).x;
            const float toolWidth = iconWidth + kToolGap + kComboWidth;
            const float frameHeight = ImGui::GetFrameHeight();
            const float toolX =
                    std::max(ImGui::GetWindowPos().x,
                             contentMaxX - kHorizontalPadding - toolWidth);
            const float toolY = cursor.y + (height - frameHeight) * 0.5f;

            ImGui::SetCursorScreenPos(ImVec2(toolX, toolY));
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
            ImGui::TextUnformatted(ICON_FA_SLIDERS);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Precise transform step");
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, kToolGap);

            std::array<const char *, kTranslatePrecisionOptions.size() + 1>
                    labels{};
            labels[0] = "Off";
            for (size_t i = 0; i < kTranslatePrecisionOptions.size(); ++i)
                labels[i + 1] = kTranslatePrecisionOptions[i].label;

            int selectedIndex =
                    state.preciseTransformEnabled
                            ? TranslatePrecisionIndex(
                                      state.preciseTranslateStepMeters) +
                                      1
                            : 0;
            ImGui::SetNextItemWidth(kComboWidth);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                ImVec2(ImGui::GetStyle().WindowPadding.x, 7.0f));
            if (Ui::Combo(theme, "##viewport_precise_translate_step",
                          &selectedIndex, labels.data(),
                          static_cast<int>(labels.size()))) {
                if (selectedIndex == 0) {
                    if (callbacks.setPreciseTransformEnabled)
                        callbacks.setPreciseTransformEnabled(false);
                } else {
                    const TranslatePrecisionOption &option =
                            kTranslatePrecisionOptions[static_cast<size_t>(
                                    selectedIndex - 1)];
                    if (callbacks.setPreciseTranslateStepMeters)
                        callbacks.setPreciseTranslateStepMeters(option.meters);
                    if (callbacks.setPreciseTransformEnabled)
                        callbacks.setPreciseTransformEnabled(true);
                }
            }
            ImGui::PopStyleVar();
        }
    } // namespace

    /** @copydoc ViewportTranslatePrecisionLabel */
    const char *ViewportTranslatePrecisionLabel(float stepMeters) {
        return kTranslatePrecisionOptions[static_cast<size_t>(
                                                  TranslatePrecisionIndex(
                                                          stepMeters))]
                .label;
    }

    /** @copydoc ResolveViewportTranslateSnapStep */
    float ResolveViewportTranslateSnapStep(bool preciseEnabled,
                                           float preciseStepMeters,
                                           float fallbackStepMeters) {
        if (!preciseEnabled)
            return fallbackStepMeters;
        return preciseStepMeters > 0.0f ? preciseStepMeters
                                        : fallbackStepMeters;
    }

    /** @copydoc ResolveViewportRotateSnapStepDegrees */
    float ResolveViewportRotateSnapStepDegrees(bool preciseEnabled,
                                               float preciseStepMeters,
                                               float fallbackStepDegrees) {
        if (!preciseEnabled || preciseStepMeters <= 0.0f)
            return fallbackStepDegrees;
        
        const auto index = static_cast<size_t>(TranslatePrecisionIndex(preciseStepMeters));
        if (std::abs(kTranslatePrecisionOptions[index].meters - preciseStepMeters) > 1e-5f)
            return fallbackStepDegrees;
            
        return kTranslatePrecisionOptions[index].rotationDegrees;
    }

    /** @copydoc ResolveViewportScaleSnapStep */
    float ResolveViewportScaleSnapStep(bool preciseEnabled,
                                       float preciseStepMeters,
                                       float fallbackStep) {
        return ResolveViewportTranslateSnapStep(preciseEnabled,
                                                preciseStepMeters,
                                                fallbackStep);
    }

    /** @copydoc EditorViewportToolbar::Draw */
    void EditorViewportToolbar::Draw(
            const Ui::EditorTheme &theme,
            const EditorViewportToolbarState &state,
            const EditorViewportToolbarCallbacks &callbacks) const {
        constexpr float kVerticalPadding = 8.0f;

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float contentMinX =
                ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
        const float contentMaxX =
                ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const float width = std::max(0.0f, contentMaxX - contentMinX);
        const float height =
                std::max(ImGui::GetTextLineHeight() + kVerticalPadding * 2.0f,
                         ImGui::GetFrameHeight() + 6.0f);

        ImGui::PushID("viewport_toolbar");
        DrawViewportToolbarFrame(theme, "Viewport", cursor, contentMinX,
                                 contentMaxX, height);
        DrawPrecisionTool(theme, state, callbacks, cursor, contentMaxX, height);
        ImGui::PopID();

        ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + height));
        ImGui::Dummy(ImVec2(width, 0.0f));
    }
} // namespace Horo::Editor
