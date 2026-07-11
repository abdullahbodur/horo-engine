#include <Horo/Editor/Localization/ILocalizationService.h>
#include "editor/screens/welcome/WelcomeScreenGui.h"

#include "Horo/Editor/EditorUiComponents.h"

#include <cfloat>

namespace Horo::Editor
{
    namespace
    {

        /// @brief Draws a single recent project card.
        /// @return true if the card was clicked this frame.
        [[nodiscard]] bool DrawProjectCard(const RecentProjectEntry &project,
                                           const int index,
                                           const EditorGuiContext &ctx)
        {
            using namespace Theme;

            bool clicked = false;
            ImGui::PushID(index);
            {
                // Invisible button over the full card area for click detection.
                const ImVec2 cardSize{-1.0F, 64.0F};
                const ImVec2 cursorBefore = ImGui::GetCursorScreenPos();

                Ui::ScopedCard card("ProjectCard", cardSize, 14.0F, 12.0F);

                auto *dl = ImGui::GetWindowDrawList();
                const ImVec2 thumbMin = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(thumbMin,
                                  {thumbMin.x + 40.0F, thumbMin.y + 40.0F},
                                  U32(Bg3()),
                                  Layout::Radius);
                ImGui::Dummy({52.0F, 40.0F});
                ImGui::SameLine(0.0F, 0.0F);

                ImGui::BeginGroup();
                {
                    ScopedTextStyle textStyle(ctx.theme.fonts.sans, 14.0F, FontPx::Sans);
                    ImGui::TextUnformatted(project.name.c_str());
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0F);
                {
                    ScopedTextStyle textStyle(ctx.theme.fonts.mono, 12.0F, FontPx::Mono);
                    ImGui::TextDisabled("%s", project.rootPath.c_str());
                }
                ImGui::EndGroup();

                float metaWidth = 0.0F;
                {
                    ScopedTextStyle textStyle(ctx.theme.fonts.mono, 11.0F, FontPx::Mono);
                    metaWidth = ImGui::CalcTextSize(project.lastOpenedLabel.c_str()).x;
                }
                ImGui::SameLine(ImGui::GetWindowWidth() - metaWidth - 14.0F);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0F);
                {
                    ScopedTextStyle textStyle(ctx.theme.fonts.mono, 11.0F, FontPx::Mono);
                    ImGui::TextDisabled("%s", project.lastOpenedLabel.c_str());
                }

                // Detect click over the card's bounding rect.
                const ImVec2 cardEnd = ImGui::GetItemRectMax();
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_None))
                {
                    const ImVec2 mouse = ImGui::GetMousePos();
                    if (mouse.x >= cursorBefore.x && mouse.x <= cardEnd.x &&
                        mouse.y >= cursorBefore.y && mouse.y <= cardEnd.y)
                    {
                        clicked = true;
                    }
                }
            }
            ImGui::PopID();
            return clicked;
        }

        void DrawNewsCard(const char *tag, const char *title, const char *description, const ImVec2 size, const EditorGuiContext &ctx)
        {
            using namespace Theme;

            Ui::ScopedCard card(title, size, 14.0F, 14.0F, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
            {
                ScopedTextStyle textStyle(ctx.theme.fonts.mono, 11.0F, FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Accent());
                ImGui::TextUnformatted(tag);
                ImGui::PopStyleColor();
            }
            {
                ScopedTextStyle textStyle(ctx.theme.fonts.sans, 14.0F, FontPx::Sans);
                ImGui::TextUnformatted(title);
            }
            ImGui::Dummy({0.0F, 2.0F});
            {
                ScopedTextStyle textStyle(ctx.theme.fonts.sans, 12.5F, FontPx::Sans);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x - 28.0F);
                ImGui::TextDisabled("%s", description);
                ImGui::PopTextWrapPos();
            }
        }

        [[nodiscard]] bool DrawWelcomeActionButton(const char *label,
                                                   const Ui::ButtonVariant variant,
                                                   const EditorGuiContext &ctx)
        {
            return Ui::Button(Ui::ButtonProps{label,
                                              ImVec2{-1.0F, 42.0F},
                                              variant,
                                              true,
                                              14.0F,
                                              ctx.theme.fonts.sans,
                                              Theme::FontPx::Sans});
        }

    } // namespace

    [[nodiscard]] WelcomeScreenGuiResult DrawWelcomeScreenGui(const WelcomeViewModel &viewModel,
                                                               const EditorGuiContext &ctx,
                                                               const WelcomeScreenGuiAssets &assets)
    {
        using namespace Theme;

        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration |
                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        WelcomeScreenGuiResult result;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Bg1());
        ImGui::Begin("Welcome", nullptr, flags);

        const ImVec2 available = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Layout::RadiusModal);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg1());
        ImGui::BeginChild("WelcomeCard", available, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);

        auto *cardDrawList = ImGui::GetWindowDrawList();
        const ImVec2 cardMin = ImGui::GetWindowPos();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{Layout::WelcomePad, Layout::WelcomePad});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
        ImGui::BeginChild("Side",
                          {Layout::WelcomeSideW, 0.0F},
                          false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_AlwaysUseWindowPadding);

        if (assets.logo != 0)
        {
            ImGui::Image(assets.logo, {64.0F, 64.0F});
        }
        ImGui::Dummy({0.0F, 18.0F});

        {
            ImFont *titleFont = ctx.theme.fonts.monoSemiBold ? ctx.theme.fonts.monoSemiBold : ImGui::GetFont();
            const ImVec2 titlePos = ImGui::GetCursorScreenPos();
            constexpr float titlePx = 24.0F;
            constexpr float titleSpacing = 2.0F;
            const char *title = "HORO";
            auto *dl = ImGui::GetWindowDrawList();
            float cursorX = titlePos.x;
            for (const char *c = title; *c != '\0'; ++c)
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
            ImFont *subtitleFont = ctx.theme.fonts.sans ? ctx.theme.fonts.sans : ImGui::GetFont();
            const ImVec2 subtitlePos = ImGui::GetCursorScreenPos();
            constexpr float subtitlePx = 12.0F;
            const std::string subtitleText = ctx.localization.Get("editor", "welcome.subtitle");
            const char *subtitle = subtitleText.c_str();
            ImGui::GetWindowDrawList()->AddText(subtitleFont, subtitlePx, subtitlePos, U32(Muted()), subtitle);
            const ImVec2 subtitleSize = subtitleFont->CalcTextSizeA(subtitlePx, FLT_MAX, 0.0F, subtitle);
            ImGui::Dummy({subtitleSize.x, subtitleSize.y});
        }
        ImGui::Dummy({0.0F, 28.0F});

        const std::string newProject = ctx.localization.Get("editor", "welcome.new_project");
        const std::string openProject = ctx.localization.Get("editor", "welcome.open_project");
        const std::string openSettings = ctx.localization.Get("editor", "welcome.open_settings");
        if (DrawWelcomeActionButton(newProject.c_str(), Ui::ButtonVariant::Primary, ctx))
        {
            result.command = WelcomeScreenGuiCommand::NewProject;
        }
        if (DrawWelcomeActionButton(openProject.c_str(), Ui::ButtonVariant::Secondary, ctx))
        {
            result.command = WelcomeScreenGuiCommand::OpenProject;
        }
        if (DrawWelcomeActionButton(openSettings.c_str(), Ui::ButtonVariant::Secondary, ctx))
        {
            result.command = WelcomeScreenGuiCommand::OpenSettings;
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        cardDrawList->AddLine({cardMin.x + Layout::WelcomeSideW, cardMin.y},
                              {cardMin.x + Layout::WelcomeSideW, cardMin.y + available.y},
                              U32(Border()),
                              1.0F);

        ImGui::SameLine(0.0F, 0.0F);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{Layout::WelcomePad, Layout::WelcomePad});
        ImGui::BeginChild("Main",
                          {0.0F, 0.0F},
                          false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_AlwaysUseWindowPadding);

        {
            ScopedTextStyle textStyle(ctx.theme.fonts.monoSemiBold, 13.0F, FontPx::MonoSemiBold);
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
                result.command = WelcomeScreenGuiCommand::OpenRecentProject;
                result.openRecentIndex = static_cast<int>(i);
            }
        }

        ImGui::Dummy({0.0F, 28.0F});

        {
            ScopedTextStyle textStyle(ctx.theme.fonts.monoSemiBold, 13.0F, FontPx::MonoSemiBold);
            const std::string whatsNewStr = ctx.localization.Get("editor", "welcome.whats_new");
            ImGui::TextDisabled("%s", whatsNewStr.c_str());
        }
        ImGui::Dummy({0.0F, 14.0F});

        const float newsWidth = (ImGui::GetContentRegionAvail().x - 12.0F) * 0.5F;
        for (int i = 0; i < static_cast<int>(viewModel.whatsNew.size()); ++i)
        {
            const auto &entry = viewModel.whatsNew[static_cast<std::size_t>(i)];
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
