#include "editor/screens/welcome/WelcomeScreenGui.h"

#include "editor/design_system/components/EditorUiComponents.h"

#include <cfloat>
#include <cstddef>

namespace Horo::Editor
{
    namespace
    {

        void DrawProjectCard(const RecentProjectEntry &project, const int index, const Theme::Fonts &fonts)
        {
            using namespace Theme;

            ImGui::PushID(index);
            {
                Ui::ScopedCard card("ProjectCard", ImVec2{-1.0F, 64.0F}, 14.0F, 12.0F);

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
                    ScopedTextStyle textStyle(fonts.sans, 15.0F, FontPx::Sans);
                    ImGui::TextUnformatted(project.name.c_str());
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0F);
                {
                    ScopedTextStyle textStyle(fonts.mono, 12.0F, FontPx::Mono);
                    ImGui::TextDisabled("%s", project.rootPath.c_str());
                }
                ImGui::EndGroup();

                float metaWidth = 0.0F;
                {
                    ScopedTextStyle textStyle(fonts.mono, 11.0F, FontPx::Mono);
                    metaWidth = ImGui::CalcTextSize(project.lastOpenedLabel.c_str()).x;
                }
                ImGui::SameLine(ImGui::GetWindowWidth() - metaWidth - 14.0F);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0F);
                {
                    ScopedTextStyle textStyle(fonts.mono, 11.0F, FontPx::Mono);
                    ImGui::TextDisabled("%s", project.lastOpenedLabel.c_str());
                }
            }
            ImGui::PopID();
        }

        void DrawNewsCard(const char *tag, const char *title, const char *description, const ImVec2 size, const Theme::Fonts &fonts)
        {
            using namespace Theme;

            Ui::ScopedCard card(title, size, 14.0F, 14.0F, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
            {
                ScopedTextStyle textStyle(fonts.mono, 11.0F, FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Accent());
                ImGui::TextUnformatted(tag);
                ImGui::PopStyleColor();
            }
            {
                ScopedTextStyle textStyle(fonts.sans, 14.0F, FontPx::Sans);
                ImGui::TextUnformatted(title);
            }
            ImGui::Dummy({0.0F, 2.0F});
            {
                ScopedTextStyle textStyle(fonts.sans, 12.5F, FontPx::Sans);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x - 28.0F);
                ImGui::TextDisabled("%s", description);
                ImGui::PopTextWrapPos();
            }
        }

        [[nodiscard]] bool DrawWelcomeActionButton(const char *label,
                                                   const Ui::ButtonVariant variant,
                                                   const Theme::Fonts &fonts)
        {
            return Ui::Button(Ui::ButtonProps{label,
                                              ImVec2{-1.0F, 42.0F},
                                              variant,
                                              true,
                                              14.0F,
                                              fonts.sans,
                                              Theme::FontPx::Sans});
        }

    } // namespace

    [[nodiscard]] WelcomeScreenGuiCommand DrawWelcomeScreenGui(const WelcomeViewModel &viewModel,
                                                               const Theme::Fonts &fonts,
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

        WelcomeScreenGuiCommand command = WelcomeScreenGuiCommand::None;

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
            ImFont *titleFont = fonts.monoSemiBold ? fonts.monoSemiBold : ImGui::GetFont();
            const ImVec2 titlePos = ImGui::GetCursorScreenPos();
            constexpr float titlePx = 24.0F;
            const char *title = "HORO";
            ImGui::GetWindowDrawList()->AddText(titleFont, titlePx, titlePos, U32(Text()), title);
            const ImVec2 titleSize = titleFont->CalcTextSizeA(titlePx, FLT_MAX, 0.0F, title);
            ImGui::Dummy({titleSize.x, titleSize.y});
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0F);
        {
            ImFont *subtitleFont = fonts.sans ? fonts.sans : ImGui::GetFont();
            const ImVec2 subtitlePos = ImGui::GetCursorScreenPos();
            constexpr float subtitlePx = 13.0F;
            const char *subtitle = "Game Engine";
            ImGui::GetWindowDrawList()->AddText(subtitleFont, subtitlePx, subtitlePos, U32(Muted()), subtitle);
            const ImVec2 subtitleSize = subtitleFont->CalcTextSizeA(subtitlePx, FLT_MAX, 0.0F, subtitle);
            ImGui::Dummy({subtitleSize.x, subtitleSize.y});
        }
        ImGui::Dummy({0.0F, 28.0F});

        if (DrawWelcomeActionButton("+  New Project", Ui::ButtonVariant::Primary, fonts))
        {
            command = WelcomeScreenGuiCommand::NewProject;
        }
        (void)DrawWelcomeActionButton("   Open Project", Ui::ButtonVariant::Secondary, fonts);
        (void)DrawWelcomeActionButton("\xe2\x86\x93  Open Recent", Ui::ButtonVariant::Secondary, fonts);
        if (DrawWelcomeActionButton("   Open Settings", Ui::ButtonVariant::Secondary, fonts))
        {
            command = WelcomeScreenGuiCommand::OpenSettings;
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
            ScopedTextStyle textStyle(fonts.monoSemiBold, 13.0F, FontPx::MonoSemiBold);
            ImGui::TextDisabled("RECENT PROJECTS");
            ImGui::SameLine(ImGui::GetWindowWidth() - 92.0F);
            ImGui::PushStyleColor(ImGuiCol_Text, Accent());
            ImGui::TextUnformatted("BROWSE ALL");
            ImGui::PopStyleColor();
        }
        ImGui::Dummy({0.0F, 14.0F});

        for (std::size_t i = 0; i < viewModel.recentProjects.size(); ++i)
        {
            DrawProjectCard(viewModel.recentProjects[i], static_cast<int>(i), fonts);
        }

        ImGui::Dummy({0.0F, 28.0F});

        {
            ScopedTextStyle textStyle(fonts.monoSemiBold, 13.0F, FontPx::MonoSemiBold);
            ImGui::TextDisabled("WHAT'S NEW");
        }
        ImGui::Dummy({0.0F, 14.0F});

        const float newsWidth = (ImGui::GetContentRegionAvail().x - 12.0F) * 0.5F;
        DrawNewsCard("Release Notes",
                     "GPU-driven rendering preview",
                     "Experimental render graph and bindless resource backend now available.",
                     {newsWidth, 104.0F},
                     fonts);
        ImGui::SameLine(0.0F, 12.0F);
        DrawNewsCard("Documentation",
                     "MCP workflow guide",
                     "Author scenes and assets through the Model Context Protocol.",
                     {newsWidth, 104.0F},
                     fonts);

        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleColor();

        return command;
    }

} // namespace Horo::Editor
