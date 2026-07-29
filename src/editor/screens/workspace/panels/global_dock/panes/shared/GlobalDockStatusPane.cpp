#include "editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.h"

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <imgui.h>
#include <string>

namespace Horo::Editor {
    namespace {
        constexpr float OuterPaddingX = 10.0F;
        constexpr float OuterPaddingY = 8.0F;
        constexpr float HeaderHeight = 28.0F;
        constexpr float HeaderGap = 8.0F;
        constexpr float RowHeight = 20.0F;

        [[nodiscard]] ImU32 ToneColor(const GlobalDockStatusTone tone) noexcept {
            switch (tone) {
                case GlobalDockStatusTone::Normal:
                    return Theme::U32(Theme::Muted());
                case GlobalDockStatusTone::Dim:
                    return Theme::U32(Theme::Dim());
                case GlobalDockStatusTone::Warning:
                    return Theme::U32(Theme::Warn());
                case GlobalDockStatusTone::Error:
                    return Theme::U32(Theme::Err());
            }
            return Theme::U32(Theme::Muted());
        }
    }  // namespace

    /** @copydoc DrawGlobalDockStatusPane */
    void DrawGlobalDockStatusPane(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context,
                                  const std::string_view descriptionKey, const std::span<const GlobalDockStatusRow> rows) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImFont *font = context.theme.fonts.sansCompact != nullptr ? context.theme.fonts.sansCompact : ImGui::GetFont();
        constexpr float fontSize = Theme::FontPx::SansCompact;
        const auto &localization = context.localization;
        const ImVec2 headerPosition{
            contentOrigin.x + OuterPaddingX,
            contentOrigin.y + OuterPaddingY,
        };
        const std::string &embedded = localization.Get("editor", "workspace.content_browser.embedded");
        const std::string &description = localization.Get("editor", descriptionKey);
        drawList->AddText(font, fontSize, headerPosition, Theme::U32(Theme::Dim()), embedded.c_str());
        const float embeddedWidth = font->CalcTextSizeA(fontSize, 1000.0F, 0.0F, embedded.c_str()).x;
        drawList->AddText(font, fontSize, {headerPosition.x + embeddedWidth + 8.0F, headerPosition.y}, Theme::U32(Theme::Muted()),
                          description.c_str());

        const float firstRowY = contentOrigin.y + OuterPaddingY + HeaderHeight + HeaderGap;
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const GlobalDockStatusRow &row = rows[index];
            const float y = firstRowY + static_cast<float>(index) * RowHeight;
            float valueX = contentOrigin.x + OuterPaddingX;
            if (!row.label.empty()) {
                drawList->AddText(font, fontSize, {valueX, y}, Theme::U32(Theme::Dim()), row.label.data(),
                                  row.label.data() + row.label.size());
                valueX +=
                    font->CalcTextSizeA(fontSize, contentWidth, 0.0F, row.label.data(), row.label.data() + row.label.size()).x + 10.0F;
            }
            const std::string &value = localization.Get("editor", row.valueKey);
            const ImVec4 clip{
                valueX,
                y,
                contentOrigin.x + OuterPaddingX + contentWidth,
                y + RowHeight,
            };
            drawList->AddText(font, fontSize, {valueX, y}, ToneColor(row.tone), value.c_str(), nullptr, 0.0F, &clip);
        }
        ImGui::SetCursorScreenPos({
            contentOrigin.x + OuterPaddingX,
            firstRowY + static_cast<float>(rows.size()) * RowHeight,
        });
        ImGui::Dummy({contentWidth, 1.0F});
    }
}  // namespace Horo::Editor
