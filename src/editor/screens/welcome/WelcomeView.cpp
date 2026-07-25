#include "editor/screens/welcome/WelcomeView.h"
#include <Horo/Editor/Localization/ILocalizationService.h>

#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/GuiScreenHost.h"

#include <cfloat>
#include <algorithm>

namespace Horo::Editor
{
    namespace
    {
        constexpr float kProjectNameFontSize = 14.0F;
        constexpr float kProjectPathFontSize = 14.0F;
        constexpr float kProjectMetaFontSize = 13.0F;
        constexpr float kNewsTagFontSize = 13.0F;
        constexpr float kNewsTitleFontSize = 15.0F;
        constexpr float kNewsBodyFontSize = 14.0F;
        constexpr float kSectionLabelFontSize = 14.0F;

        [[nodiscard]] const char* CompatibilityLabelKey(const RecentProjectCompatibilityProjection& projection)
        {
            if (projection.inspectionState == RecentProjectInspectionState::Refreshing)
                return "welcome.project.compatibility.refreshing";
            using enum Application::ProjectCompatibilityStatus;
            switch (projection.status)
            {
            case Current: return "welcome.project.compatibility.current";
            case CompatibleReleaseLine: return "welcome.project.compatibility.compatible";
            case AutomaticMigrationRequired: return "welcome.project.compatibility.will_upgrade";
            case RecoveryRequired: return "welcome.project.compatibility.recovery_required";
            case FutureVersion: return "welcome.project.compatibility.newer_horo_required";
            case MigrationPathMissing:
            case RequiredProviderUnavailable: return "welcome.project.compatibility.cannot_upgrade";
            case Corrupt:
            case Inaccessible: return "welcome.project.compatibility.version_unavailable";
            }
            return "welcome.project.compatibility.version_unavailable";
        }

        /// @brief Draws a single recent project card.
        /// @return true if the card was clicked this frame.
        [[nodiscard]] bool DrawProjectCard(const RecentProjectEntry& project, const int index,
                                           const EditorGuiContext& ctx)
        {
            using namespace Theme;

            bool clicked = false;
            ImGui::PushID(index);
            {
                Ui::ScopedCard card("ProjectCard", {-1.0F, 64.0F}, 14.0F, 12.0F);

                const ImVec2 contentMin = ImGui::GetCursorScreenPos();
                constexpr float contentHeight = 40.0F;
                const float contentWidth = ImGui::GetContentRegionAvail().x;
                clicked = ImGui::InvisibleButton(
                    "Project card###welcome_project_card", {contentWidth, contentHeight});

                auto* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(contentMin, {contentMin.x + contentHeight, contentMin.y + contentHeight},
                                        U32(Bg3()), Layout::Radius);

                std::string versionText;
                std::string statusText = project.lastOpenedLabel;
                if (project.compatibility.has_value())
                {
                    versionText = project.compatibility->projectVersion.has_value()
                                      ? "Horo " + Application::FormatHoroVersion(
                                                       project.compatibility->projectVersion->value)
                                      : ctx.localization.Get(
                                            "editor", "welcome.project.compatibility.version_unavailable");
                    statusText = ctx.localization.Get("editor", CompatibilityLabelKey(*project.compatibility));
                }

                ImFont* const nameFont = ctx.theme.fonts.sans ? ctx.theme.fonts.sans : ImGui::GetFont();
                ImFont* const compactFont =
                    ctx.theme.fonts.sansCompact ? ctx.theme.fonts.sansCompact : ImGui::GetFont();
                const float metaWidth = std::max(
                    compactFont->CalcTextSizeA(kProjectMetaFontSize, FLT_MAX, 0.0F, versionText.c_str()).x,
                    compactFont->CalcTextSizeA(kProjectMetaFontSize, FLT_MAX, 0.0F, statusText.c_str()).x);
                const float contentMaxX = contentMin.x + contentWidth;
                const float visibleMetaWidth = std::min(metaWidth, contentWidth * 0.38F);
                const float metaMinX = contentMaxX - visibleMetaWidth;
                constexpr float textGap = 12.0F;
                const float detailsMinX = contentMin.x + contentHeight + textGap;
                const float detailsMaxX = std::max(detailsMinX, metaMinX - textGap);
                const ImVec4 detailsClip{detailsMinX, contentMin.y, detailsMaxX, contentMin.y + contentHeight};
                const ImVec4 metaClip{metaMinX, contentMin.y, contentMaxX, contentMin.y + contentHeight};

                drawList->AddText(nameFont, kProjectNameFontSize, {detailsMinX, contentMin.y}, U32(Text()),
                                  project.name.c_str(), nullptr, 0.0F, &detailsClip);
                drawList->AddText(compactFont, kProjectPathFontSize, {detailsMinX, contentMin.y + 21.0F}, U32(Muted()),
                                  project.rootPath.c_str(), nullptr, 0.0F, &detailsClip);

                const float metaY = contentMin.y + (versionText.empty() ? 11.0F : 0.0F);
                if (!versionText.empty())
                {
                    drawList->AddText(compactFont, kProjectMetaFontSize, {metaMinX, metaY}, U32(Muted()),
                                      versionText.c_str(), nullptr, 0.0F, &metaClip);
                }
                drawList->AddText(compactFont, kProjectMetaFontSize,
                                  {metaMinX, metaY + (versionText.empty() ? 0.0F : 21.0F)}, U32(Muted()),
                                  statusText.c_str(), nullptr, 0.0F, &metaClip);
            }
            ImGui::PopID();
            return clicked;
        }

        void DrawNewsCard(const char* tag, const char* title, const char* description, const ImVec2 size,
                          const EditorGuiContext& ctx)
        {
            using namespace Theme;

            Ui::ScopedCard card(title, size, 14.0F, 14.0F, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
            {
                ScopedTextStyle textStyle(ctx.theme.fonts.sansCompact, kNewsTagFontSize, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Accent());
                ImGui::TextUnformatted(tag);
                ImGui::PopStyleColor();
            }
            {
                ScopedTextStyle textStyle(ctx.theme.fonts.sans, kNewsTitleFontSize, FontPx::Sans);
                ImGui::TextUnformatted(title);
            }
            ImGui::Dummy({0.0F, 2.0F});
            {
                ScopedTextStyle textStyle(ctx.theme.fonts.sans, kNewsBodyFontSize, FontPx::Sans);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x - 28.0F);
                ImGui::TextDisabled("%s", description);
                ImGui::PopTextWrapPos();
            }
        }

        [[nodiscard]] bool DrawWelcomeActionButton(const char* label, const Ui::ButtonVariant variant,
                                                   const EditorGuiContext& ctx)
        {
            return Ui::Button(
                Ui::ButtonProps{
                    label, ImVec2{-1.0F, 42.0F}, variant, true, 14.0F, ctx.theme.fonts.sans, Theme::FontPx::Sans
                });
        }
    } // namespace

    [[nodiscard]] WelcomeViewResult DrawWelcomeView(const WelcomeViewModel& viewModel, const EditorGuiContext& ctx,
                                                    const WelcomeViewAssets& assets,
                                                    const GuiContentRegion& contentRegion)
    {
        using namespace Theme;

        ImGui::SetNextWindowPos(ImVec2{contentRegion.x, contentRegion.y});
        ImGui::SetNextWindowSize(ImVec2{contentRegion.width, contentRegion.height});
        constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        WelcomeViewResult result;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Bg1());
        ImGui::Begin("Welcome", nullptr, flags);

        const ImVec2 available = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Layout::RadiusModal);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg1());
        ImGui::BeginChild("WelcomeCard", available, true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);

        auto* cardDrawList = ImGui::GetWindowDrawList();
        const ImVec2 cardMin = ImGui::GetWindowPos();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{Layout::WelcomePad, Layout::WelcomePad});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
        ImGui::BeginChild("Side", {Layout::WelcomeSideW, 0.0F}, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_AlwaysUseWindowPadding);

        if (assets.logo != 0)
        {
            ImGui::Image(assets.logo, {64.0F, 64.0F});
        }
        ImGui::Dummy({0.0F, 18.0F});

        {
            ImFont* titleFont = ctx.theme.fonts.sansEmphasis ? ctx.theme.fonts.sansEmphasis : ImGui::GetFont();
            const ImVec2 titlePos = ImGui::GetCursorScreenPos();
            constexpr float titlePx = 24.0F;
            constexpr float titleSpacing = 2.0F;
            const char* title = "HORO";
            auto* dl = ImGui::GetWindowDrawList();
            float cursorX = titlePos.x;
            for (const char* c = title; *c != '\0'; ++c)
            {
                const std::string glyph{*c};
                dl->AddText(titleFont, titlePx, {cursorX, titlePos.y}, U32(Text()), glyph.c_str());
                cursorX += titleFont->CalcTextSizeA(titlePx, FLT_MAX, 0.0F, glyph.c_str()).x + titleSpacing;
            }
            const ImVec2 titleSize = titleFont->CalcTextSizeA(titlePx, FLT_MAX, 0.0F, title);
            const float titleWidth = titleSize.x + titleSpacing * 3.0F;
            ImGui::Dummy({titleWidth, titleSize.y});
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0F);
        {
            ImFont* subtitleFont = ctx.theme.fonts.sans ? ctx.theme.fonts.sans : ImGui::GetFont();
            const ImVec2 subtitlePos = ImGui::GetCursorScreenPos();
            constexpr float subtitlePx = 12.0F;
            const std::string subtitleText = ctx.localization.Get("editor", "welcome.subtitle");
            const char* subtitle = subtitleText.c_str();
            ImGui::GetWindowDrawList()->AddText(subtitleFont, subtitlePx, subtitlePos, U32(Muted()), subtitle);
            const ImVec2 subtitleSize = subtitleFont->CalcTextSizeA(subtitlePx, FLT_MAX, 0.0F, subtitle);
            ImGui::Dummy({subtitleSize.x, subtitleSize.y});
        }
        ImGui::Dummy({0.0F, 28.0F});

        const std::string newProject =
            ctx.localization.Get("editor", "welcome.new_project") + "###welcome_new_project";
        const std::string openProject =
            ctx.localization.Get("editor", "welcome.open_project") + "###welcome_open_project";
        const std::string openSettings =
            ctx.localization.Get("editor", "welcome.open_settings") + "###welcome_open_settings";
        if (DrawWelcomeActionButton(newProject.c_str(), Ui::ButtonVariant::Primary, ctx))
        {
            result.command = WelcomeViewCommand::NewProject;
        }
        if (DrawWelcomeActionButton(openProject.c_str(), Ui::ButtonVariant::Secondary, ctx))
        {
            result.command = WelcomeViewCommand::OpenProject;
        }
        if (DrawWelcomeActionButton(openSettings.c_str(), Ui::ButtonVariant::Secondary, ctx))
        {
            result.command = WelcomeViewCommand::OpenSettings;
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        cardDrawList->AddLine({cardMin.x + Layout::WelcomeSideW, cardMin.y},
                              {cardMin.x + Layout::WelcomeSideW, cardMin.y + available.y}, U32(Border()), 1.0F);

        ImGui::SameLine(0.0F, 0.0F);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{Layout::WelcomePad, Layout::WelcomePad});
        ImGui::BeginChild("Main", {0.0F, 0.0F}, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_AlwaysUseWindowPadding);

        {
            ScopedTextStyle textStyle(ctx.theme.fonts.sansEmphasis, kSectionLabelFontSize, FontPx::SansEmphasis);
            const std::string recentProjects = ctx.localization.Get("editor", "welcome.recent_projects");
            ImGui::TextDisabled("%s", recentProjects.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 92.0F);
            ImGui::PushStyleColor(ImGuiCol_Text, Accent());
            const std::string browseAll = ctx.localization.Get("editor", "welcome.browse_all");
            ImGui::TextUnformatted(browseAll.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Dummy({0.0F, 14.0F});

        for (std::size_t i = 0; i < viewModel.recentProjects.size(); ++i)
        {
            if (DrawProjectCard(viewModel.recentProjects[i], static_cast<int>(i), ctx))
            {
                result.command = WelcomeViewCommand::OpenRecentProject;
                result.openRecentIndex = static_cast<int>(i);
            }
        }

        ImGui::Dummy({0.0F, 28.0F});

        {
            ScopedTextStyle textStyle(ctx.theme.fonts.sansEmphasis, kSectionLabelFontSize, FontPx::SansEmphasis);
            const std::string whatsNewStr = ctx.localization.Get("editor", "welcome.whats_new");
            ImGui::TextDisabled("%s", whatsNewStr.c_str());
        }
        ImGui::Dummy({0.0F, 14.0F});

        const float newsWidth = (ImGui::GetContentRegionAvail().x - 12.0F) * 0.5F;
        for (int i = 0; i < static_cast<int>(viewModel.whatsNew.size()); ++i)
        {
            const auto& entry = viewModel.whatsNew[static_cast<std::size_t>(i)];
            DrawNewsCard(entry.tag, entry.title, entry.body, {newsWidth, 104.0F}, ctx);
            if (i == 0)
            {
                ImGui::SameLine(0.0F, 12.0F);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleColor();

        return result;
    }
} // namespace Horo::Editor
