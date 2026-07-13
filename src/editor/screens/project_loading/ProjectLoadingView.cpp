#include "ProjectLoadingView.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <format>

#include <imgui.h>
#include <imgui_internal.h>

namespace Horo::Editor
{
ProjectLoadingViewCommand DrawProjectLoadingView(ProjectLoadingViewState &state, const EditorGuiContext &ctx,
                                                 const GuiContentRegion &contentRegion)
{
    ProjectLoadingViewCommand cmd = ProjectLoadingViewCommand::None;

    // Cover the entire screen with the canvas background
    ImGui::SetNextWindowPos(ImVec2{contentRegion.x, contentRegion.y});
    ImGui::SetNextWindowSize(ImVec2{contentRegion.width, contentRegion.height});
    constexpr auto windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
    ImGui::Begin("ProjectLoadingCanvas", nullptr, windowFlags);

    // Center the modal box
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    constexpr auto modalSize = ImVec2(480.0f, 200.0f);

    ImGui::SetCursorPos({(canvasSize.x - modalSize.x) * 0.5f, (canvasSize.y - modalSize.y) * 0.5f});

    // Modal Styling
    constexpr float kModalPadding = 32.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kModalPadding, kModalPadding));

    constexpr auto childFlags =
        ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("LoadingModal", modalSize, true, childFlags);

    // Header
    {
        Theme::ScopedTextStyle tsTitle(ctx.theme.fonts.sansEmphasis, 18.0f, Theme::FontPx::SansEmphasis);
        const std::string openingFmt = ctx.localization.Get("editor", "project_loading.title.opening");
        std::string titleText = openingFmt + " '" + state.projectName + "'";
        ImGui::TextUnformatted(titleText.c_str());
    }

    ImGui::Dummy({0.0f, 4.0f});

    // Status Text & Percentage
    {
        Theme::ScopedTextStyle tsStatus(ctx.theme.fonts.sans, 13.0f, Theme::FontPx::Sans);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
        ImGui::TextUnformatted(state.statusText.c_str());
        ImGui::PopStyleColor();

        // Percentage on the right, flush with the modal's right padding.
        std::string pctText = std::format("{}%", static_cast<int>(state.progress));
        const float pctWidth = ImGui::CalcTextSize(pctText.c_str()).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - pctWidth - kModalPadding);

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent());
        ImGui::TextUnformatted(pctText.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy({0.0f, 8.0f});

    // Progress Bar
    {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg2());
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, state.isCancelled ? Theme::Err() : Theme::Accent());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        // Draw progress bar without overlay text
        ImGui::ProgressBar(state.progress / 100.0f, ImVec2(-1.0f, 6.0f), "");

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    ImGui::Dummy({0.0f, 24.0f});

    // Footer Actions
    {
        constexpr auto kButtonSize = ImVec2(80.0f, 32.0f);

        // Pin flush to the bottom-right, using the SAME right margin as the
        // percentage label above (kModalPadding) instead of a hardcoded
        // magic number — keeps both elements visually aligned to one
        // consistent right edge, and stays correct if modalSize/padding
        // ever change.
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - kModalPadding - kButtonSize.x);

        const auto cancelLabel = ctx.localization.Get("editor", "project_loading.action.cancel");
        if (Ui::Button({.label = cancelLabel.c_str(),
                        .size = {kButtonSize.x, kButtonSize.y},
                        .variant = Ui::ButtonVariant::Secondary,
                        .fontSize = 13.0F,
                        .font = ctx.theme.fonts.sansCompact,
                        .baseFontSize = Theme::FontPx::SansCompact}))
        {
            state.isCancelled = true;
            state.statusText = ctx.localization.Get("editor", "project_loading.status.cancelling");
            cmd = ProjectLoadingViewCommand::Cancel;
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::End();           // ProjectLoadingCanvas
    ImGui::PopStyleColor(); // Bg0

    return cmd;
}
} // namespace Horo::Editor
