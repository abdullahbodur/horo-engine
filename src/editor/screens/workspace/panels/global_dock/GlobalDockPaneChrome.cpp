#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneChrome.h"

#include "editor/screens/workspace/panels/global_dock/GlobalDockPaneLayout.h"

#include <algorithm>
#include <cfloat>
#include <string>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] ImFont *ResolveFont(ImFont *preferred) noexcept {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] float TextWidth(ImFont *font, const float size, const std::string_view text) {
            if (text.empty())
                return 0.0F;
            return ResolveFont(font)->CalcTextSizeA(size, FLT_MAX, 0.0F, text.data(), text.data() + text.size()).x;
        }
    }  // namespace

    ImVec4 GlobalDockToneColor(const GlobalDockTone tone) noexcept {
        switch (tone) {
            case GlobalDockTone::Accent:
                return Theme::Accent();
            case GlobalDockTone::Positive:
                return Theme::Ok();
            case GlobalDockTone::Warning:
                return Theme::Warn();
            case GlobalDockTone::Error:
                return Theme::Err();
            case GlobalDockTone::Neutral:
                return Theme::Muted();
        }
        return Theme::Muted();
    }

    void DrawGlobalDockToolbarSurface(const ImVec2 origin, const float width, const float height) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, Theme::U32(Theme::BottomDockToolbarSurface()));
        drawList->AddLine({origin.x, origin.y + height - 1.0F}, {origin.x + width, origin.y + height - 1.0F}, Theme::U32(Theme::Border()));
    }

    void DrawGlobalDockTableHeaderSurface(const ImVec2 origin, const float width, const float height) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, Theme::U32(Theme::BottomDockContentSurface()));
        drawList->AddLine({origin.x, origin.y + height - 1.0F}, {origin.x + width, origin.y + height - 1.0F}, Theme::U32(Theme::Border()));
    }

    void DrawGlobalDockTableRowSurface(const ImVec2 origin, const float width, const float height, const bool hovered) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        if (hovered)
            drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, Theme::U32(Theme::Hover()));
        drawList->AddLine({origin.x, origin.y + height - 1.0F}, {origin.x + width, origin.y + height - 1.0F},
                          Theme::U32(Theme::ConsoleRowBorder()));
    }

    void DrawGlobalDockMetricCard(const ImVec2 origin, const ImVec2 size, const GlobalDockMetricCardProps &props,
                                  const Theme::Fonts &fonts) {
        const float scale = Theme::GetActiveTokens().sizes.uiScale;
        const float padding = 10.0F * scale;
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 maximum{origin.x + size.x, origin.y + size.y};
        drawList->AddRectFilled(origin, maximum, Theme::U32(Theme::BottomDockMetricSurface()), Theme::GetActiveTokens().radii.card);
        drawList->AddRect(origin, maximum, Theme::U32(Theme::Border()), Theme::GetActiveTokens().radii.card);

        ImFont *labelFont = ResolveFont(fonts.sans);
        ImFont *valueFont = ResolveFont(fonts.sansCompact);
        const float labelSize = Theme::TextPx::Caption();
        const float valueSize = Theme::TextPx::CardTitle();
        drawList->AddText(labelFont, labelSize, {origin.x + padding, origin.y + padding}, Theme::U32(Theme::Muted()), props.label.data(),
                          props.label.data() + props.label.size());
        const float valueY = origin.y + padding + labelSize + 3.0F * scale;
        const ImVec4 valueColor = props.valueTone == GlobalDockTone::Neutral ? Theme::Text() : GlobalDockToneColor(props.valueTone);
        drawList->AddText(valueFont, valueSize, {origin.x + padding, valueY}, Theme::U32(valueColor), props.value.data(),
                          props.value.data() + props.value.size());
        if (!props.note.empty()) {
            const float noteX = origin.x + padding + TextWidth(valueFont, valueSize, props.value) + 4.0F * scale;
            const ImVec4 noteColor = props.noteTone == GlobalDockTone::Neutral ? Theme::Muted() : GlobalDockToneColor(props.noteTone);
            drawList->AddText(labelFont, labelSize, {noteX, valueY + valueSize - labelSize}, Theme::U32(noteColor), props.note.data(),
                              props.note.data() + props.note.size());
        }

        if (props.samples.size() < 2U)
            return;
        const float chartTop = valueY + valueSize + 5.0F * scale;
        const float chartBottom = maximum.y - padding;
        const float chartWidth = std::max(1.0F, size.x - padding * 2.0F);
        const float chartHeight = std::max(1.0F, chartBottom - chartTop);
        if (props.budget.has_value()) {
            const float budgetY = chartBottom - std::clamp(*props.budget, 0.0F, 1.0F) * chartHeight;
            const ImU32 budgetColor = Theme::U32(GlobalDockToneColor(GlobalDockTone::Warning));
            for (float dashX = origin.x + padding; dashX < maximum.x - padding; dashX += 6.0F * scale)
                drawList->AddLine({dashX, budgetY}, {std::min(dashX + 3.0F * scale, maximum.x - padding), budgetY}, budgetColor);
        }
        for (std::size_t index = 1; index < props.samples.size(); ++index) {
            const float x0 =
                origin.x + padding + chartWidth * static_cast<float>(index - 1U) / static_cast<float>(props.samples.size() - 1U);
            const float x1 = origin.x + padding + chartWidth * static_cast<float>(index) / static_cast<float>(props.samples.size() - 1U);
            const float y0 = chartBottom - std::clamp(props.samples[index - 1U], 0.0F, 1.0F) * chartHeight;
            const float y1 = chartBottom - std::clamp(props.samples[index], 0.0F, 1.0F) * chartHeight;
            drawList->AddLine({x0, y0}, {x1, y1}, Theme::U32(Theme::Accent()), 1.5F * scale);
        }
    }

    void DrawGlobalDockMeter(const ImVec2 origin, const float width, const float progress) {
        const float scale = Theme::GetActiveTokens().sizes.uiScale;
        const float height = 6.0F * scale;
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, Theme::U32(Theme::BottomDockControlSurface()),
                                height * 0.5F);
        const float fill = std::clamp(progress, 0.0F, 1.0F) * width;
        if (fill <= 0.0F)
            return;
        drawList->AddRectFilledMultiColor(origin, {origin.x + fill, origin.y + height}, Theme::U32(Theme::Accent()),
                                          Theme::U32(GlobalDockToneColor(GlobalDockTone::Positive)),
                                          Theme::U32(GlobalDockToneColor(GlobalDockTone::Positive)), Theme::U32(Theme::Accent()));
    }

    void DrawGlobalDockFooterSurface(const ImVec2 origin, const float width, const float height) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, Theme::U32(Theme::BottomDockToolbarSurface()));
        drawList->AddLine(origin, {origin.x + width, origin.y}, Theme::U32(Theme::Border()));
    }

    float MeasureGlobalDockToolbarChip(const GlobalDockToolbarChipProps &props, const Theme::Fonts &fonts) {
        const GlobalDockPaneMetrics metrics = ResolveGlobalDockPaneMetrics();
        const float fontSize = Theme::TextPx::Label();
        float width = metrics.toolbarGap * 2.0F + TextWidth(fonts.sansCompact, fontSize, props.label);
        if (props.icon != Ui::UiIcon::None)
            width += 14.0F * Theme::GetActiveTokens().sizes.uiScale + metrics.toolbarGap;
        if (props.count.has_value())
            width += metrics.toolbarGap + TextWidth(fonts.sansCompact, Theme::TextPx::Caption(), std::to_string(*props.count));
        return std::max(metrics.controlHeight, width);
    }

    bool DrawGlobalDockToolbarChip(const ImVec2 origin, const float width, const GlobalDockToolbarChipProps &props,
                                   const Theme::Fonts &fonts) {
        const GlobalDockPaneMetrics metrics = ResolveGlobalDockPaneMetrics();
        const float scale = Theme::GetActiveTokens().sizes.uiScale;
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushID(props.id);
        const bool clicked = ImGui::InvisibleButton("##chip", {width, metrics.controlHeight});
        const bool pressed = clicked && !props.disabled;
        const bool hovered = ImGui::IsItemHovered() && !props.disabled;
        ImGui::PopID();

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 maximum{origin.x + width, origin.y + metrics.controlHeight};
        drawList->AddRectFilled(origin, maximum, Theme::U32(hovered ? Theme::Hover() : Theme::BottomDockControlSurface()),
                                Theme::GetActiveTokens().radii.control);
        drawList->AddRect(origin, maximum, Theme::U32(props.active ? Theme::Accent() : Theme::Border()),
                          Theme::GetActiveTokens().radii.control);
        if (props.active) {
            const float inset = std::max(1.0F, scale);
            drawList->AddRect({origin.x + inset, origin.y + inset}, {maximum.x - inset, maximum.y - inset}, Theme::U32(Theme::AccentSoft()),
                              std::max(0.0F, Theme::GetActiveTokens().radii.control - inset));
        }

        float x = origin.x + metrics.toolbarGap;
        if (props.icon != Ui::UiIcon::None) {
            const float iconSize = 14.0F * scale;
            Ui::DrawEditorIcon(drawList, props.icon, {x, origin.y + (metrics.controlHeight - iconSize) * 0.5F}, {iconSize, iconSize},
                               Theme::U32(GlobalDockToneColor(props.tone)), fonts.icon);
            x += iconSize + metrics.toolbarGap;
        }
        ImFont *font = ResolveFont(fonts.sansCompact);
        const float labelSize = Theme::TextPx::Label();
        const ImVec4 labelColor = props.disabled    ? Theme::Dim()
                                  : props.toneLabel ? GlobalDockToneColor(props.tone)
                                  : props.active    ? Theme::Text()
                                                    : Theme::Muted();
        drawList->AddText(font, labelSize, {x, origin.y + (metrics.controlHeight - labelSize) * 0.5F}, Theme::U32(labelColor),
                          props.label.data(), props.label.data() + props.label.size());
        if (props.count.has_value()) {
            const std::string count = std::to_string(*props.count);
            const float countSize = Theme::TextPx::Caption();
            const float countWidth = TextWidth(font, countSize, count);
            drawList->AddText(font, countSize,
                              {maximum.x - metrics.toolbarGap - countWidth, origin.y + (metrics.controlHeight - countSize) * 0.5F},
                              Theme::U32(GlobalDockToneColor(props.tone)), count.c_str());
        }
        return pressed;
    }

    void DrawGlobalDockToolbarSeparator(const float x, const float y) {
        const GlobalDockPaneMetrics metrics = ResolveGlobalDockPaneMetrics();
        const float inset = 5.0F * Theme::GetActiveTokens().sizes.uiScale;
        ImGui::GetWindowDrawList()->AddLine({x, y + inset}, {x, y + metrics.controlHeight - inset}, Theme::U32(Theme::Border()));
    }

    float DrawGlobalDockStatePill(const ImVec2 origin, const std::string_view label, const GlobalDockTone tone, const Theme::Fonts &fonts) {
        const float scale = Theme::GetActiveTokens().sizes.uiScale;
        const float fontSize = Theme::TextPx::Caption();
        const float height = 22.0F * scale;
        const float dotSize = 6.0F * scale;
        const float padding = 6.0F * scale;
        const float gap = 5.0F * scale;
        const float width = padding * 2.0F + dotSize + gap + TextWidth(fonts.sansCompact, fontSize, label);
        ImVec4 surface = GlobalDockToneColor(tone);
        surface.w = tone == GlobalDockTone::Neutral ? 0.08F : 0.10F;
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, Theme::U32(surface), height * 0.5F);
        const ImVec4 color = GlobalDockToneColor(tone);
        drawList->AddCircleFilled({origin.x + padding + dotSize * 0.5F, origin.y + height * 0.5F}, dotSize * 0.5F, Theme::U32(color));
        drawList->AddText(ResolveFont(fonts.sansCompact), fontSize,
                          {origin.x + padding + dotSize + gap, origin.y + (height - fontSize) * 0.5F}, Theme::U32(color), label.data(),
                          label.data() + label.size());
        return width;
    }

    void DrawGlobalDockProgressBar(const ImVec2 origin, const float width, const float progress) {
        const float scale = Theme::GetActiveTokens().sizes.uiScale;
        const float height = 5.0F * scale;
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, Theme::U32(Theme::BottomDockControlSurface()),
                                height * 0.5F);
        const float fill = std::clamp(progress, 0.0F, 1.0F) * width;
        if (fill > 0.0F)
            drawList->AddRectFilled(origin, {origin.x + fill, origin.y + height}, Theme::U32(Theme::Accent()), height * 0.5F);
    }

    void DrawGlobalDockClippedText(ImDrawList &drawList, ImFont *font, const float fontSize, const ImVec2 minimum, const ImVec2 maximum,
                                   const ImVec4 color, const std::string_view text) {
        if (text.empty() || maximum.x <= minimum.x)
            return;
        drawList.PushClipRect(minimum, maximum, true);
        drawList.AddText(ResolveFont(font), fontSize, minimum, Theme::U32(color), text.data(), text.data() + text.size());
        drawList.PopClipRect();
    }
}  // namespace Horo::Editor
