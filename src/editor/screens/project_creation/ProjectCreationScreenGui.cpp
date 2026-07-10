#include "editor/screens/project_creation/ProjectCreationScreenGui.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Foundation/Logging/Logger.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor
{
    namespace
    {
        using Theme::Fonts;
        using Theme::ScopedTextStyle;
        using Ui::ScopedCard;

        constexpr std::array<const char *, 6> kTemplateNames = {
            "Empty", "3D Starter", "First Person", "Package Based", "Tech Demo", "Custom"
        };
        constexpr std::array<const char *, 6> kTemplateIds = {
            "empty", "3d-starter", "first-person", "package-based", "tech-demo", "custom"
        };

        namespace WizardLayout
        {
            constexpr float ModalW = 900.0F;
            constexpr float ModalH = 680.0F;
            constexpr float ViewportPad = 56.0F;

            constexpr float HeaderH = 58.0F;
            constexpr float FooterH = 52.0F;
            constexpr float SidebarW = 220.0F;

            constexpr float HeaderPadX = 22.0F;
            constexpr float SidebarPadX = 14.0F;
            constexpr float SidebarPadY = 18.0F;
            constexpr float MainPadX = 28.0F;
            constexpr float MainPadY = 24.0F;

            constexpr float StepH = 62.0F;
            constexpr float StepGap = 6.0F;

            constexpr float TemplateGap = 12.0F;
            constexpr float TemplateH = 116.0F;
            constexpr float TemplatePad = 14.0F;
            constexpr float TemplateIconPx = 24.0F;
            constexpr float TemplateNamePx = 17.0F;
            constexpr float TemplateDescPx = 14.0F;

            constexpr float GridGap = 16.0F;
            constexpr float FieldLabelGap = 5.0F;
            constexpr float HintGap = 3.0F;
            constexpr float CardPad = 18.0F;
            constexpr float CardGap = 18.0F;
            constexpr float CheckGap = 12.0F;

            constexpr float Radius = 4.0F;
            constexpr float TemplateRadius = 6.0F;
            constexpr float ModalRadius = 8.0F;
        } // namespace WizardLayout



        void CopyDraftText(char *destination, const std::size_t capacity, const std::string &source)
        {
            const std::size_t count = std::min(capacity - 1, source.size());
            std::memcpy(destination, source.data(), count);
            destination[count] = '\0';
        }

        [[nodiscard]] int FindTemplateIndex(const std::string &templateId)
        {
            for (std::size_t i = 0; i < kTemplateIds.size(); ++i)
            {
                if (templateId == kTemplateIds[i])
                {
                    return static_cast<int>(i);
                }
            }
            return 1; // Default to 3D Starter
        }

        void SynchronizePresentation(ProjectCreationController &controller, ProjectCreationScreenGuiState &state)
        {
            if (state.initialized)
            {
                return;
            }
            const ProjectCreationDraft &draft = controller.Draft();
            CopyDraftText(state.projectName, sizeof(state.projectName), draft.projectName);
            CopyDraftText(state.projectPath, sizeof(state.projectPath), draft.projectPath);
            CopyDraftText(state.projectVersion, sizeof(state.projectVersion), draft.projectVersion);
            CopyDraftText(state.defaultScene, sizeof(state.defaultScene), draft.defaultScene);
            CopyDraftText(state.targetFps, sizeof(state.targetFps), std::to_string(draft.targetFrameRate));

            if (draft.renderBackend == "opengl")
                state.renderBackendIndex = 0;
            else if (draft.renderBackend == "vulkan")
                state.renderBackendIndex = 1;
            else
                state.renderBackendIndex = 2;

            state.physicsIndex = draft.physicsEnabled ? 0 : 1;

            if (draft.buildProfile == "desktop-debug")
                state.buildProfileIndex = 0;
            else if (draft.buildProfile == "desktop-profile")
                state.buildProfileIndex = 1;
            else
                state.buildProfileIndex = 2;

            if (draft.assetCompression == "lz4")
                state.assetCompressionIndex = 0;
            else if (draft.assetCompression == "none")
                state.assetCompressionIndex = 1;
            else
                state.assetCompressionIndex = 2;

            if (draft.textureCompression == "bc7")
                state.textureCompressionIndex = 0;
            else if (draft.textureCompression == "bc5")
                state.textureCompressionIndex = 1;
            else if (draft.textureCompression == "astc")
                state.textureCompressionIndex = 2;
            else
                state.textureCompressionIndex = 3;

            if (draft.targetPlatform == "host")
                state.targetPlatformIndex = 0;
            else if (draft.targetPlatform == "windows")
                state.targetPlatformIndex = 1;
            else if (draft.targetPlatform == "linux")
                state.targetPlatformIndex = 2;
            else
                state.targetPlatformIndex = 3;

            if (draft.compilerFamily == "default")
                state.compilerFamilyIndex = 0;
            else if (draft.compilerFamily == "clang")
                state.compilerFamilyIndex = 1;
            else if (draft.compilerFamily == "gcc")
                state.compilerFamilyIndex = 2;
            else
                state.compilerFamilyIndex = 3;

            state.cppStandardIndex = (draft.minimumCxxStandard == 20) ? 0 : 1;
            state.initialized = true;
        }

        [[nodiscard]] bool DrawFolderIconButton(const char *id,
                                                const float width,
                                                const Fonts &f,
                                                const bool error = false)
        {
            ImGui::PushID(id);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            const float height = ImGui::GetFrameHeight();
            ImGui::PopStyleVar();

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size{width, height};
            const bool clicked = ImGui::InvisibleButton("##btn", size);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            auto *dl = ImGui::GetWindowDrawList();
            const ImU32 bgCol = Theme::U32(active ? Theme::AccentSoft() : (hovered ? Theme::Hover() : Theme::Bg3()));
            const ImU32 borderCol = Theme::U32(error ? Theme::Err() : (active || hovered ? Theme::Accent() : Theme::Border()));
            const float rounding = WizardLayout::Radius;

            dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, bgCol, rounding);
            dl->AddRect(pos, {pos.x + size.x, pos.y + size.y}, borderCol, rounding, 0, (active || hovered) ? 1.5F : 1.0F);

            const float iconW = 18.0F;
            const float iconH = 14.0F;
            const float ox = pos.x + (size.x - iconW) * 0.5F;
            const float oy = pos.y + (size.y - iconH) * 0.5F;
            const ImU32 iconCol = Theme::U32((active || hovered) ? Theme::Accent() : Theme::Text());

            dl->AddLine({ox, oy + 2.0F}, {ox + 6.0F, oy + 2.0F}, iconCol, 1.5F);
            dl->AddLine({ox + 6.0F, oy + 2.0F}, {ox + 8.0F, oy + 4.0F}, iconCol, 1.5F);
            dl->AddRect({ox, oy + 4.0F}, {ox + iconW, oy + iconH}, iconCol, 2.0F, 0, 1.5F);
            dl->AddLine({ox, oy + 6.5F}, {ox + iconW, oy + 6.5F}, iconCol, 1.25F);

            ImGui::PopID();
            return clicked;
        }

        [[nodiscard]] std::optional<std::filesystem::path> OpenFolderSelectionDialog(const char *prompt)
        {
#if defined(__APPLE__)
            std::string cmd = "osascript -e 'POSIX path of (choose folder with prompt \"";
            for (char c : std::string_view(prompt ? prompt : "Select Folder"))
            {
                if (c == '"' || c == '\\' || c == '\'')
                    cmd += '\\';
                cmd += c;
            }
            cmd += "\")' 2>/dev/null";
            FILE *pipe = popen(cmd.c_str(), "r");
            if (!pipe)
                return std::nullopt;
            char buffer[1024];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
            {
                result += buffer;
            }
            int status = pclose(pipe);
            if (status != 0 || result.empty())
                return std::nullopt;
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            {
                result.pop_back();
            }
            if (result.empty())
                return std::nullopt;
            return std::filesystem::path(result);
#elif defined(__linux__)
            std::string cmd = "zenity --file-selection --directory --title=\"";
            for (char c : std::string_view(prompt ? prompt : "Select Folder"))
            {
                if (c == '"' || c == '\\' || c == '\'')
                    cmd += '\\';
                cmd += c;
            }
            cmd += "\" 2>/dev/null";
            FILE *pipe = popen(cmd.c_str(), "r");
            if (!pipe)
                return std::nullopt;
            char buffer[1024];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
            {
                result += buffer;
            }
            int status = pclose(pipe);
            if (status != 0 || result.empty())
                return std::nullopt;
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            {
                result.pop_back();
            }
            if (result.empty())
                return std::nullopt;
            return std::filesystem::path(result);
#elif defined(_WIN32)
            std::string cmd = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $f = New-Object System.Windows.Forms.FolderBrowserDialog; $f.Description = '";
            for (char c : std::string_view(prompt ? prompt : "Select Folder"))
            {
                if (c == '\'' || c == '"')
                    cmd += '`';
                cmd += c;
            }
            cmd += "'; if($f.ShowDialog() -eq 'OK'){ $f.SelectedPath }\" 2>nul";
            FILE *pipe = _popen(cmd.c_str(), "r");
            if (!pipe)
                return std::nullopt;
            char buffer[1024];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
            {
                result += buffer;
            }
            int status = _pclose(pipe);
            if (status != 0 || result.empty())
                return std::nullopt;
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            {
                result.pop_back();
            }
            if (result.empty())
                return std::nullopt;
            return std::filesystem::path(result);
#else
            static_cast<void>(prompt);
            return std::nullopt;
#endif
        }

        bool DrawInputField(const char *label,
                            char *buffer,
                            const size_t bufferSize,
                            const float width,
                            const Fonts &f,
                            const char *hint = nullptr,
                            const bool error = false,
                            const char *errorText = nullptr)
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();
            Ui::FieldLabel(label, f);
            if (width != 0.0F)
            {
                ImGui::PushItemWidth(width);
            }
            bool changed = Ui::InputTextControl("##value", buffer, bufferSize, f, error);
            if (width != 0.0F)
            {
                ImGui::PopItemWidth();
            }
            if (error && errorText)
            {
                Ui::ErrorText(errorText, f);
            }
            else if (hint)
            {
                Ui::Hint(hint, f);
            }
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        bool DrawComboField(const char *label,
                            int *value,
                            const char *const items[],
                            const int itemCount,
                            const float width,
                            const Fonts &f,
                            const char *hint = nullptr)
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();
            Ui::FieldLabel(label, f);
            if (width != 0.0F)
            {
                ImGui::PushItemWidth(width);
            }
            bool changed = Ui::ComboControl("##value", value, items, itemCount, f);
            if (width != 0.0F)
            {
                ImGui::PopItemWidth();
            }
            if (hint)
            {
                Ui::Hint(hint, f);
            }
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        void CheckboxCss(const char *label, bool *value, const Fonts &f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{8.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WizardLayout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
            ImGui::PushStyleColor(ImGuiCol_CheckMark, Theme::Accent());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
            {
                ScopedTextStyle ts(f.sans, 13.0F, Theme::FontPx::Sans);
                ImGui::Checkbox(label, value);
            }
            ImGui::PopStyleColor(6);
            ImGui::PopStyleVar(4);
        }

        void DrawNewProjectBackdrop(const ImGuiViewport *vp, const ImVec2 modalPos, const ImVec2 modalSize)
        {
            auto *dl = ImGui::GetBackgroundDrawList();

            dl->AddRectFilled(vp->WorkPos,
                              {vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y},
                              IM_COL32(0, 0, 0, 90));

            constexpr int shadowLayers = 18;
            for (int i = shadowLayers; i >= 1; --i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(shadowLayers);
                const float spread = 80.0F * t;
                const float alpha = 0.55F * (1.0F - t) * 0.075F;
                const ImVec4 col{0.0F, 0.0F, 0.0F, alpha};
                dl->AddRectFilled({modalPos.x - spread, modalPos.y + 28.0F - spread},
                                  {modalPos.x + modalSize.x + spread, modalPos.y + modalSize.y + 28.0F + spread},
                                  Theme::U32(col),
                                  WizardLayout::ModalRadius + spread);
            }
        }

        void DrawWizardHeader(ProjectCreationController &controller,
                              ProjectCreationScreenGuiState &st,
                              const Fonts &f,
                              const ImTextureID logo,
                              ProjectCreationScreenGuiCommand &outCommand)
        {
            using namespace Theme;
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("WizHdr", {0, HeaderH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 headerPos = ImGui::GetWindowPos();
            const float headerW = ImGui::GetWindowWidth();

            ImGui::SetCursorPos({HeaderPadX, 12.0F});
            if (logo != 0)
            {
                ImGui::Image(logo, {20.0F, 20.0F});
                ImGui::SameLine(0.0F, 9.0F);
            }
            {
                ScopedTextStyle ts(f.monoSemiBold, 14.0F, FontPx::MonoSemiBold);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted("NEW PROJECT");
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorPos({HeaderPadX, 36.0F});
            {
                ScopedTextStyle ts(f.mono, 12.0F, FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::TextUnformatted("Create portable .horo metadata and starter content");
                ImGui::PopStyleColor();
            }

            const ImVec2 closeSize{38.0F, 36.0F};
            ImGui::SetCursorPos({headerW - HeaderPadX - closeSize.x, 11.0F});
            if (Ui::IconCloseButton("close-new-project", closeSize))
            {
                if (controller.LeaveIntent() == ProjectCreationLeaveIntent::RequireDiscardConfirmation)
                {
                    st.confirmingDiscard = true;
                }
                else
                {
                    outCommand = ProjectCreationScreenGuiCommand::ReturnToWelcome;
                }
            }

            auto *dl = ImGui::GetWindowDrawList();
            dl->AddLine({headerPos.x, headerPos.y + HeaderH - 1.0F},
                        {headerPos.x + headerW, headerPos.y + HeaderH - 1.0F},
                        Theme::U32(Theme::Border()),
                        1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawWizardSidebar(ProjectCreationScreenGuiState &st, const Fonts &f, const float sideH)
        {
            using namespace Theme;
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg2());
            ImGui::BeginChild("WizSide", {SidebarW, sideH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 sidePos = ImGui::GetWindowPos();
            auto *dl = ImGui::GetWindowDrawList();

            static constexpr std::array<const char *, 4> kStepLabels = {"Template", "Identity", "Settings", "Review"};
            static constexpr std::array<const char *, 4> kStepDescs = {
                "Choose starter", "Name & location", "Runtime defaults", "Validate & create"
            };

            ImGui::SetCursorPos({SidebarPadX, SidebarPadY});

            for (int s = 1; s <= 4; ++s)
            {
                ImGui::PushID(s);
                const bool active = (st.step == s);
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const ImVec2 rowSize{SidebarW - SidebarPadX * 2.0F, StepH};

                if (active)
                {
                    dl->AddRectFilled(rowMin,
                                      {rowMin.x + rowSize.x, rowMin.y + rowSize.y},
                                      Theme::U32(Theme::AccentSoft()),
                                      Radius);
                }

                ImGui::InvisibleButton("##step", rowSize);
                if (ImGui::IsItemClicked())
                {
                    st.step = s;
                }

                const ImVec2 circleCenter{rowMin.x + 10.0F + 11.0F, rowMin.y + 11.0F + 11.0F};
                dl->AddCircleFilled(circleCenter, 11.0F, Theme::U32(active ? Theme::Accent() : Theme::Bg3()), 24);
                dl->AddCircle(circleCenter, 11.0F, Theme::U32(active ? Theme::Accent() : Theme::Border()), 24, 1.0F);

                static constexpr std::array<const char *, 5> kStepNumbers = {"", "1", "2", "3", "4"};
                const char *number = kStepNumbers[s];
                ImFont *numberFont = f.mono ? f.mono : ImGui::GetFont();
                const float numberFontSize = 13.0F;
                const ImVec2 numberSize = numberFont->CalcTextSizeA(numberFontSize, FLT_MAX, 0.0F, number);
                dl->AddText(numberFont,
                            numberFontSize,
                            {circleCenter.x - numberSize.x * 0.5F, circleCenter.y - numberSize.y * 0.5F},
                            Theme::U32(active ? Theme::DarkText() : Theme::Dim()),
                            number);

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 7.0F});
                {
                    ScopedTextStyle ts(f.sans, 17.0F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
                    ImGui::TextUnformatted(kStepLabels[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 34.0F});
                {
                    ScopedTextStyle ts(f.mono, 14.0F, FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted(kStepDescs[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x, rowMin.y + StepH + StepGap});
                ImGui::PopID();
            }

            dl->AddLine({sidePos.x + SidebarW - 1.0F, sidePos.y},
                        {sidePos.x + SidebarW - 1.0F, sidePos.y + sideH},
                        Theme::U32(Theme::Border()),
                        1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawStepTemplate(ProjectCreationController &controller, ProjectCreationScreenGuiState &st, const Fonts &f)
        {
            using namespace Theme;
            using namespace WizardLayout;

            static constexpr std::array<const char *, 6> kDescs = {
                "No starter scene. Minimal asset tree and project.json.",
                "Scene, camera, directional light, floor, material defaults.",
                "Character controller, input map, capsule, and test level.",
                "Create from a verified template package and lockfile.",
                "Rendering samples, observability overlays, benchmark scene.",
                "Pick systems manually before project generation."
            };

            Ui::SectionTitle("CHOOSE A TEMPLATE", f);
            ImGui::Dummy({0.0F, 14.0F});

            const float cardW = (ImGui::GetContentRegionAvail().x - TemplateGap * 2.0F) / 3.0F;
            const int currentTemplateIndex = FindTemplateIndex(controller.Draft().templateId);

            for (int i = 0; i < 6; ++i)
            {
                if (i > 0 && i % 3 == 0)
                {
                    ImGui::Dummy({0.0F, TemplateGap});
                }
                else if (i % 3 != 0)
                {
                    ImGui::SameLine(0.0F, TemplateGap);
                }

                ImGui::PushID(i);
                const bool selected = (currentTemplateIndex == i);

                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, TemplateRadius);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{TemplatePad, TemplatePad});
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? Theme::AccentSoft() : Theme::Bg2());
                ImGui::PushStyleColor(ImGuiCol_Border, selected ? Theme::Accent() : Theme::Border());

                ImGui::BeginChild("TemplateCard",
                                  {cardW, TemplateH},
                                  true,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_AlwaysUseWindowPadding);

                const ImVec2 iconPos = ImGui::GetCursorScreenPos();
                auto *dl = ImGui::GetWindowDrawList();
                const ImU32 iconColor = Theme::U32(selected ? Theme::Accent() : Theme::Text());

                if (i == 0)
                {
                    const float s = 18.0F;
                    const float o = (TemplateIconPx - s) * 0.5F;
                    dl->AddRect({iconPos.x + o, iconPos.y + o},
                                {iconPos.x + o + s, iconPos.y + o + s},
                                iconColor, 3.0F, 0, 2.0F);
                }
                else if (i == 1)
                {
                    const float cx = iconPos.x + 12.0F;
                    const float cy = iconPos.y + 12.0F;
                    dl->AddRect({iconPos.x + 4.0F, iconPos.y + 6.0F}, {iconPos.x + 20.0F, iconPos.y + 18.0F}, iconColor, 2.0F, 0, 1.5F);
                    dl->AddLine({cx, iconPos.y + 6.0F}, {cx, iconPos.y + 18.0F}, iconColor, 1.5F);
                    dl->AddLine({iconPos.x + 4.0F, cy}, {iconPos.x + 20.0F, cy}, iconColor, 1.5F);
                }
                else if (i == 2)
                {
                    const float cx = iconPos.x + 12.0F;
                    dl->AddCircle({cx, iconPos.y + 6.0F}, 3.5F, iconColor, 12, 1.5F);
                    dl->AddRect({cx - 7.0F, iconPos.y + 12.0F}, {cx + 7.0F, iconPos.y + 20.0F}, iconColor, 4.0F, 0, 1.5F);
                }
                else if (i == 3)
                {
                    dl->AddRect({iconPos.x + 4.0F, iconPos.y + 6.0F}, {iconPos.x + 20.0F, iconPos.y + 18.0F}, iconColor, 2.0F, 0, 1.5F);
                    dl->AddLine({iconPos.x + 4.0F, iconPos.y + 10.0F}, {iconPos.x + 20.0F, iconPos.y + 10.0F}, iconColor, 1.5F);
                    dl->AddLine({iconPos.x + 12.0F, iconPos.y + 10.0F}, {iconPos.x + 12.0F, iconPos.y + 18.0F}, iconColor, 1.5F);
                }
                else if (i == 4)
                {
                    dl->AddRect({iconPos.x + 3.0F, iconPos.y + 5.0F}, {iconPos.x + 21.0F, iconPos.y + 17.0F}, iconColor, 2.0F, 0, 1.5F);
                    dl->AddLine({iconPos.x + 8.0F, iconPos.y + 20.0F}, {iconPos.x + 16.0F, iconPos.y + 20.0F}, iconColor, 1.5F);
                    dl->AddLine({iconPos.x + 12.0F, iconPos.y + 17.0F}, {iconPos.x + 12.0F, iconPos.y + 20.0F}, iconColor, 1.5F);
                }
                else if (i == 5)
                {
                    dl->AddLine({iconPos.x + 4.0F, iconPos.y + 7.0F}, {iconPos.x + 20.0F, iconPos.y + 7.0F}, iconColor, 1.5F);
                    dl->AddCircleFilled({iconPos.x + 9.0F, iconPos.y + 7.0F}, 2.0F, iconColor);
                    dl->AddLine({iconPos.x + 4.0F, iconPos.y + 12.0F}, {iconPos.x + 20.0F, iconPos.y + 12.0F}, iconColor, 1.5F);
                    dl->AddCircleFilled({iconPos.x + 15.0F, iconPos.y + 12.0F}, 2.0F, iconColor);
                    dl->AddLine({iconPos.x + 4.0F, iconPos.y + 17.0F}, {iconPos.x + 20.0F, iconPos.y + 17.0F}, iconColor, 1.5F);
                    dl->AddCircleFilled({iconPos.x + 11.0F, iconPos.y + 17.0F}, 2.0F, iconColor);
                }

                ImGui::Dummy({TemplateIconPx, TemplateIconPx});
                ImGui::Dummy({0.0F, 8.0F});

                {
                    ImFont *nameFont = f.sans ? f.sans : ImGui::GetFont();
                    const ImVec2 namePos = ImGui::GetCursorScreenPos();
                    const char *name = kTemplateNames[i];
                    ImGui::GetWindowDrawList()->AddText(nameFont, TemplateNamePx, namePos, Theme::U32(Theme::Text()), name);
                    const ImVec2 nameSize = nameFont->CalcTextSizeA(TemplateNamePx, FLT_MAX, 0.0F, name);
                    ImGui::Dummy({nameSize.x, nameSize.y});
                }

                ImGui::Dummy({0.0F, 4.0F});

                {
                    ImFont *descFont = f.mono ? f.mono : ImGui::GetFont();
                    const ImVec2 descPos = ImGui::GetCursorScreenPos();
                    const float wrapW = cardW - TemplatePad * 2.0F;
                    const char *desc = kDescs[i];
                    ImGui::GetWindowDrawList()->AddText(descFont, TemplateDescPx, descPos, Theme::U32(Theme::Muted()), desc, nullptr, wrapW);
                    const ImVec2 descSize = descFont->CalcTextSizeA(TemplateDescPx, FLT_MAX, wrapW, desc);
                    ImGui::Dummy({wrapW, descSize.y});
                }

                ImGui::EndChild();

                const bool hovered = ImGui::IsItemHovered();
                if (hovered || selected)
                {
                    {
                        const ImVec2 min = ImGui::GetItemRectMin();
                        const ImVec2 max = ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddRect(
                            {min.x - (selected ? 1.0F : 0.0F), min.y - (selected ? 1.0F : 0.0F)},
                            {max.x + (selected ? 1.0F : 0.0F), max.y + (selected ? 1.0F : 0.0F)},
                            Theme::U32(hovered && !selected ? Theme::BorderStrong() : Theme::Accent()),
                            TemplateRadius,
                            0,
                            selected ? 1.5F : 1.0F);
                    }
                }

                if (ImGui::IsItemClicked())
                {
                    controller.SetTemplateId(kTemplateIds[i]);
                }

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(3);
                ImGui::PopID();
            }
        }

        const ProjectCreationDiagnostic *FindDiagnostic(const ProjectCreationValidation &validation,
                                                        const ProjectCreationDiagnosticCode code)
        {
            for (const auto &diag : validation.diagnostics)
            {
                if (diag.code == code)
                    return &diag;
            }
            return nullptr;
        }

        void DrawStepIdentity(ProjectCreationController &controller,
                              ProjectCreationScreenGuiState &st,
                              const Fonts &f,
                              const ProjectCreationValidation &validation)
        {
            using namespace WizardLayout;

            const auto *nameErrRequired = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectNameRequired);
            const auto *nameErrSep = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectNameContainsPathSeparator);
            const auto *nameErr = nameErrRequired ? nameErrRequired : nameErrSep;

            const auto *pathErrReq = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathRequired);
            const auto *pathErrOcc = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathOccupied);
            const auto *pathErrDir = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathNotDirectory);
            const auto *pathErrAcc = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathInaccessible);
            const auto *pathErrWrt = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectParentNotWritable);
            const auto *pathErr = pathErrReq ? pathErrReq : (pathErrOcc ? pathErrOcc : (pathErrDir ? pathErrDir : (pathErrAcc ? pathErrAcc : pathErrWrt)));

            Ui::SectionTitle("PROJECT IDENTITY", f);
            ImGui::Dummy({0.0F, 14.0F});

            DrawInputField("PROJECT NAME",
                           st.projectName,
                           sizeof(st.projectName),
                           -1.0F,
                           f,
                           "Stored as project.json name; projectId is generated once.",
                           nameErr != nullptr,
                           nameErr ? nameErr->message.c_str() : nullptr);
            controller.SetProjectName(st.projectName);

            ImGui::Dummy({0.0F, GridGap});

            {
                ImGui::PushID("PROJECT LOCATION");
                ImGui::BeginGroup();
                Ui::FieldLabel("PROJECT LOCATION", f);

                const float btnW = 38.0F;
                const float gapW = 8.0F;
                const float inputW = ImGui::GetContentRegionAvail().x - (btnW + gapW);

                if (inputW > 0.0F)
                {
                    ImGui::PushItemWidth(inputW);
                }
                (void)Ui::InputTextControl("##value", st.projectPath, sizeof(st.projectPath), f, pathErr != nullptr);
                if (inputW > 0.0F)
                {
                    ImGui::PopItemWidth();
                }
                controller.SetProjectPath(st.projectPath);

                ImGui::SameLine(0.0F, gapW);
                if (DrawFolderIconButton("##browse_location", btnW, f, pathErr != nullptr))
                {
                    if (auto selectedDir = OpenFolderSelectionDialog("Select Project Location"))
                    {
                        if (sizeof(st.projectPath) > 0)
                        {
                            std::filesystem::path finalPath = *selectedDir;
                            if (st.projectName[0] != '\0' && finalPath.filename().string() != st.projectName)
                            {
                                finalPath /= st.projectName;
                            }
                            const std::string pathStr = finalPath.string();
                            std::strncpy(st.projectPath, pathStr.c_str(), sizeof(st.projectPath) - 1);
                            st.projectPath[sizeof(st.projectPath) - 1] = '\0';
                            controller.SetProjectPath(st.projectPath);
                        }
                    }
                }

                if (pathErr && pathErr->message.c_str())
                {
                    Ui::ErrorText(pathErr->message.c_str(), f);
                }
                else
                {
                    Ui::Hint("Choose an empty folder or a missing location with a writable parent.", f);
                }
                ImGui::EndGroup();
                ImGui::PopID();
            }

            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("PROJECT VERSION",
                           st.projectVersion,
                           sizeof(st.projectVersion),
                           -1.0F,
                           f,
                           "Game/product version. Does not select project-format migrations.");
            controller.SetProjectVersion(st.projectVersion);

            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("DEFAULT SCENE", st.defaultScene, sizeof(st.defaultScene), -1.0F, f);
            controller.SetDefaultScene(st.defaultScene);

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("DirCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2());
                Ui::SectionTitle("PROJECT DIRECTORY", f);
                ImGui::Dummy({0.0F, 6.0F});
                ScopedTextStyle ts(f.mono, 13.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted(
                    "MyGame/\n"
                    "  .horo/\n"
                    "    project.json          \xe2\x86\x90 identity & settings\n"
                    "    plugins.json          \xe2\x86\x90 portable plugin deps\n"
                    "    editor workspace.json \xe2\x86\x90 local UI state (not committed)\n"
                    "    asset index.json      \xe2\x86\x90 derived lookup (not committed)\n"
                    "    local/                \xe2\x86\x90 machine overrides (not committed)\n"
                    "  assets/                 \xe2\x86\x90 source assets\n"
                    "    models/ textures/ materials/ shaders/ scenes/\n"
                    "  src/                    \xe2\x86\x90 optional game code\n"
                    "  build/                  \xe2\x86\x90 generated output (not committed)");
                ImGui::PopStyleColor();
            }
        }

        void DrawStepSettings(ProjectCreationController &controller, ProjectCreationScreenGuiState &st, const Fonts &f)
        {
            using namespace WizardLayout;
            static constexpr std::array<const char *, 3> kRenderBackend = {"opengl", "vulkan", "auto detect"};
            static constexpr std::array<const char *, 2> kPhysics = {"Enabled", "Disabled"};
            static constexpr std::array<const char *, 3> kBuildProfile = {"desktop-debug", "desktop-profile", "desktop-release"};
            static constexpr std::array<const char *, 3> kAssetCompression = {"lz4", "none", "zstd"};
            static constexpr std::array<const char *, 4> kTextureCompression = {"bc7", "bc5", "astc", "none"};
            static constexpr std::array<const char *, 4> kPlatform = {"host", "windows", "linux", "macos"};
            static constexpr std::array<const char *, 4> kCompiler = {"default", "clang", "gcc", "msvc"};
            static constexpr std::array<const char *, 2> kCppStd = {"C++20", "C++17"};

            const std::string &templateId = controller.Draft().templateId;
            if (templateId == "package-based")
            {
                ScopedCard card("TcPkg", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("PACKAGE SOURCE CONFIGURATION", f);
                ImGui::Dummy({0.0F, 8.0F});
                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;
                DrawInputField("TEMPLATE PACKAGE URL / REGISTRY", st.packageRegistryUrl, sizeof(st.packageRegistryUrl), colW, f);
                ImGui::SameLine(0.0F, GridGap);
                DrawInputField("PACKAGE VERSION / TAG", st.packageVersion, sizeof(st.packageVersion), colW, f);
                ImGui::Dummy({0.0F, 8.0F});
                Ui::Hint("Specify the remote registry package and version lockfile to scaffold this project.", f);
            }
            else if (templateId == "first-person")
            {
                ScopedCard card("TcFp", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("FIRST PERSON CONTROLLER SETTINGS", f);
                ImGui::Dummy({0.0F, 8.0F});
                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;
                static constexpr std::array<const char *, 3> kInputMaps = {"QWERTY / Mouse", "AZERTY / Mouse", "Gamepad (XInput/SDL)"};
                DrawComboField("CHARACTER INPUT MAP", &st.firstPersonInputMapIndex, kInputMaps.data(), static_cast<int>(kInputMaps.size()), colW, f);
                ImGui::Dummy({0.0F, 8.0F});
                Ui::Hint("A first-person camera and kinematic character capsule will be generated in defaultScene.", f);
            }
            else if (templateId == "tech-demo")
            {
                ScopedCard card("TcDemo", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("TECH DEMO CONFIGURATION", f);
                ImGui::Dummy({0.0F, 8.0F});
                CheckboxCss("Enable runtime observability and FPS overlays by default", &st.demoObservabilityOverlays, f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Include high-detail benchmark scene and camera animation track", &st.demoBenchmarkScene, f);
            }
            else if (templateId == "custom")
            {
                ScopedCard card("TcCustom", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("CUSTOM SUBSYSTEM SELECTION", f);
                ImGui::Dummy({0.0F, 8.0F});
                CheckboxCss("Rendering Subsystem (Vulkan/OpenGL core pipeline)", &st.customSubsystems[0], f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Physics Subsystem (Collision, Raycasting, Rigidbodies)", &st.customSubsystems[1], f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Audio Subsystem (3D Spatial Audio & Mixer)", &st.customSubsystems[2], f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("UI Subsystem (ImGui & Retained Scene UI)", &st.customSubsystems[3], f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Networking Subsystem (Replication & Socket Transport)", &st.customSubsystems[4], f);
            }

            if (templateId == "package-based" || templateId == "first-person" || templateId == "tech-demo" || templateId == "custom")
            {
                ImGui::Dummy({0.0F, CardGap});
            }

            {
                ScopedCard card("RtCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("RUNTIME DEFAULTS", f);
                ImGui::Dummy({0.0F, 8.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                DrawComboField("RENDER BACKEND", &st.renderBackendIndex, kRenderBackend.data(), static_cast<int>(kRenderBackend.size()), colW, f,
                               "Default: opengl. Override per host profile.");
                controller.SetRenderBackend(kRenderBackend[st.renderBackendIndex]);

                ImGui::SameLine(0.0F, GridGap);
                DrawInputField("TARGET FRAME RATE", st.targetFps, sizeof(st.targetFps), colW, f);
                controller.SetTargetFrameRate(std::atoi(st.targetFps));

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("PHYSICS", &st.physicsIndex, kPhysics.data(), static_cast<int>(kPhysics.size()), colW, f);
                controller.SetPhysicsEnabled(st.physicsIndex == 0);

                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("BUILD PROFILE", &st.buildProfileIndex, kBuildProfile.data(), static_cast<int>(kBuildProfile.size()), colW, f);
                controller.SetBuildProfile(kBuildProfile[st.buildProfileIndex]);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("ASSET COMPRESSION", &st.assetCompressionIndex, kAssetCompression.data(), static_cast<int>(kAssetCompression.size()), colW, f);
                controller.SetAssetCompression(kAssetCompression[st.assetCompressionIndex]);

                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("TEXTURE COMPRESSION", &st.textureCompressionIndex, kTextureCompression.data(), static_cast<int>(kTextureCompression.size()), colW, f);
                controller.SetTextureCompression(kTextureCompression[st.textureCompressionIndex]);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("TcCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("REQUIRED TOOLCHAIN", f);
                ImGui::Dummy({0.0F, 8.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                DrawComboField("TARGET PLATFORM", &st.targetPlatformIndex, kPlatform.data(), static_cast<int>(kPlatform.size()), colW, f);
                controller.SetTargetPlatform(kPlatform[st.targetPlatformIndex]);

                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("COMPILER FAMILY", &st.compilerFamilyIndex, kCompiler.data(), static_cast<int>(kCompiler.size()), colW, f);
                controller.SetCompilerFamily(kCompiler[st.compilerFamilyIndex]);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("MINIMUM C++ STANDARD", &st.cppStandardIndex, kCppStd.data(), static_cast<int>(kCppStd.size()), colW, f);
                controller.SetMinimumCxxStandard(st.cppStandardIndex == 0 ? 20 : 17);

                ImGui::Dummy({0.0F, 10.0F});
                Ui::Hint("Portable project settings describe build intent. Machine-specific paths and SDK "
                           "locations are resolved by user-level toolchain profiles, never stored in project.json.",
                           f);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("OptCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("OPTIONAL", f);
                ImGui::Dummy({0.0F, 10.0F});

                bool initGit = controller.Draft().initializeGit;
                CheckboxCss("Initialize git repository", &initGit, f);
                controller.SetInitializeGit(initGit);

                ImGui::Dummy({0.0F, CheckGap});
                bool restorePkgs = controller.Draft().restorePackages;
                CheckboxCss("Restore packages after creation", &restorePkgs, f);
                controller.SetRestorePackages(restorePkgs);

                ImGui::Dummy({0.0F, CheckGap});
                bool inclStarter = controller.Draft().includeStarterContent;
                CheckboxCss("Include starter content", &inclStarter, f);
                controller.SetIncludeStarterContent(inclStarter);

                ImGui::Dummy({0.0F, CheckGap});
                bool genCMake = controller.Draft().generateCMakeProject;
                CheckboxCss("Generate CMake project files", &genCMake, f);
                controller.SetGenerateCMakeProject(genCMake);
            }
        }

        void SummaryRow(const char *label, const std::string &value, const Fonts &f, const bool warn, const bool last = false)
        {
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            constexpr float rowH = 26.0F;
            constexpr float textYOffset = 4.0F;

            ImGui::SetCursorScreenPos({rowStart.x, rowStart.y + textYOffset});

            {
                ScopedTextStyle ts(f.sans, 13.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }

            float valueW = 0.0F;
            {
                ScopedTextStyle ts(f.mono, 12.0F, Theme::FontPx::Mono);
                valueW = ImGui::CalcTextSize(value.c_str()).x;
            }

            ImGui::SameLine(std::max(0.0F, rowW - valueW));
            {
                ScopedTextStyle ts(f.mono, 12.0F, Theme::FontPx::Mono);
                if (warn)
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warn());
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted(value.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({rowStart.x, rowStart.y + rowH});
            if (!last)
            {
                auto *dl = ImGui::GetWindowDrawList();
                constexpr float kDashStep = 7.0F;
                constexpr float kDashLen = 4.0F;
                const int steps = static_cast<int>(std::ceil(rowW / kDashStep));
                for (int i = 0; i < steps; ++i)
                {
                    const float x0 = rowStart.x + static_cast<float>(i) * kDashStep;
                    const float x1 = std::min(rowStart.x + rowW, x0 + kDashLen);
                    dl->AddLine({x0, rowStart.y + rowH - 1.0F}, {x1, rowStart.y + rowH - 1.0F}, Theme::U32(Theme::Border()), 1.0F);
                }
            }
        }

        void DrawStepReview(ProjectCreationController &controller,
                            const ProjectCreationValidation &validation,
                            const Fonts &f,
                            const ProjectCreationScreenGuiState &st)
        {
            using namespace WizardLayout;
            const ProjectCreationDraft &draft = controller.Draft();

            {
                ScopedCard card("RevCard1", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("PROJECT SETTINGS", f);
                ImGui::Dummy({0.0F, 6.0F});

                const int templateIdx = FindTemplateIndex(draft.templateId);
                SummaryRow("Template", kTemplateNames[templateIdx], f, false);
                SummaryRow("Project Name", draft.projectName, f, false);
                SummaryRow("Project Path", draft.projectPath, f, !validation.IsValid());
                SummaryRow("Version", draft.projectVersion, f, false);
                SummaryRow("Default Scene", draft.defaultScene, f, false);
                SummaryRow("Render Backend", draft.renderBackend, f, false);
                SummaryRow("Physics", draft.physicsEnabled ? "Enabled" : "Disabled", f, false);
                const bool hasExtraRows = (draft.templateId == "package-based" || draft.templateId == "first-person" || draft.templateId == "tech-demo" || draft.templateId == "custom");
                SummaryRow("Build Profile", draft.buildProfile, f, false, !hasExtraRows);

                if (draft.templateId == "package-based")
                {
                    SummaryRow("Package Registry", st.packageRegistryUrl, f, false);
                    SummaryRow("Package Version", st.packageVersion, f, false, true);
                }
                else if (draft.templateId == "first-person")
                {
                    static constexpr std::array<const char *, 3> kInputMaps = {"QWERTY / Mouse", "AZERTY / Mouse", "Gamepad (XInput/SDL)"};
                    SummaryRow("Input Map", kInputMaps[st.firstPersonInputMapIndex], f, false, true);
                }
                else if (draft.templateId == "tech-demo")
                {
                    SummaryRow("Observability", st.demoObservabilityOverlays ? "Enabled" : "Disabled", f, false);
                    SummaryRow("Benchmark Scene", st.demoBenchmarkScene ? "Included" : "Excluded", f, false, true);
                }
                else if (draft.templateId == "custom")
                {
                    SummaryRow("Subsystems", "Rendering, Physics, Audio", f, false, true);
                }
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("RevCard2", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                Ui::SectionTitle("WHAT WILL BE CREATED", f);
                ImGui::Dummy({0.0F, 6.0F});
                SummaryRow("Portable metadata (commit)", ".horo/project.json, .horo/plugins.json, asset sidecars", f, false);
                SummaryRow("Local / derived (ignore)", ".horo/editor workspace.json, .horo/asset index.json, .horo/local/", f, false);
                SummaryRow("Build output (ignore)", "build/", f, false);
                SummaryRow("Project schema", "formatVersion 1 \xC2\xB7 projectId generated", f, false);

                std::string validationText = "Ready to create";
                if (!validation.IsValid() && !validation.diagnostics.empty())
                {
                    validationText = "Edit \xE2\x80\x94 " + validation.diagnostics.front().message;
                }
                SummaryRow("Validation mode", validationText, f, !validation.IsValid());
                SummaryRow("Recommended .gitignore", ".horo/{editor workspace,asset index}.json .horo/local/ build/", f, false, true);
            }
        }

        void DrawWizardFooter(ProjectCreationController &controller,
                              ProjectCreationScreenGuiState &st,
                              const Fonts &f,
                              const ProjectCreationValidation &validation,
                              ProjectCreationScreenGuiCommand &outCommand)
        {
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("WizFtr", {0, FooterH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 footerPos = ImGui::GetWindowPos();
            const float footerW = ImGui::GetWindowWidth();
            auto *dl = ImGui::GetWindowDrawList();

            dl->AddLine({footerPos.x, footerPos.y},
                        {footerPos.x + footerW, footerPos.y},
                        Theme::U32(Theme::Border()),
                        1.0F);

            const ImVec2 dotCenter{footerPos.x + 22.0F + 4.0F, footerPos.y + 26.0F};
            const bool isValid = validation.IsValid();
            dl->AddCircleFilled(dotCenter, 4.0F, Theme::U32(isValid ? Theme::Ok() : Theme::Err()), 16);

            ImGui::SetCursorPos({38.0F, 18.0F});
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                const int templateIdx = FindTemplateIndex(controller.Draft().templateId);
                if (!isValid && !validation.diagnostics.empty())
                {
                    ImGui::Text("Template: %s \xC2\xB7 %s", kTemplateNames[templateIdx], validation.diagnostics.front().message.c_str());
                }
                else
                {
                    ImGui::Text("Template: %s", kTemplateNames[templateIdx]);
                }
                ImGui::PopStyleColor();
            }

            const bool isReview = (st.step == 4);
            const float backW = 80.0F;
            const float nextW = 80.0F;
            const float createW = 130.0F;
            const float btnH = 32.0F;
            const float gap = 8.0F;
            const float actionsW = isReview ? (backW + gap + createW) : (backW + gap + nextW);
            ImGui::SetCursorPos({footerW - 22.0F - actionsW, 10.0F});

            if (Ui::Button({"\xE2\x86\x90 Back", {backW, btnH}, Ui::ButtonVariant::Secondary, st.step > 1, 13.0F, f.mono, Theme::FontPx::Mono}))
            {
                st.step--;
            }

            ImGui::SameLine(0.0F, gap);

            if (!isReview)
            {
                if (Ui::Button({"Next \xE2\x86\x92", {nextW, btnH}, Ui::ButtonVariant::Primary, true, 13.0F, f.mono, Theme::FontPx::Mono}))
                {
                    st.step++;
                }
            }
            else
            {
                if (Ui::Button({"Create Project", {createW, btnH}, Ui::ButtonVariant::Primary, true, 13.0F, f.mono, Theme::FontPx::Mono}))
                {
                    if (isValid)
                    {
                        (void)controller.BuildCreationRequest();
                        outCommand = ProjectCreationScreenGuiCommand::CreateProject;
                    }
                    else if (!validation.diagnostics.empty())
                    {
                        LOG_ERROR("editor.project_creation", "Cannot create project: %s", validation.diagnostics.front().message.c_str());
                    }
                    else
                    {
                        LOG_ERROR("editor.project_creation", "Cannot create project: validation failed.");
                    }
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

    } // namespace

    /** @copydoc DrawProjectCreationScreenGui */
    ProjectCreationScreenGuiCommand DrawProjectCreationScreenGui(ProjectCreationController &controller,
                                                                 ProjectCreationScreenGuiState &state,
                                                                 const Fonts &fonts,
                                                                 const ImTextureID logo)
    {
        SynchronizePresentation(controller, state);

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        const float modalW = std::min(WizardLayout::ModalW, std::max(320.0F, vp->WorkSize.x - WizardLayout::ViewportPad));
        const float modalH = std::min(WizardLayout::ModalH, std::max(320.0F, vp->WorkSize.y - WizardLayout::ViewportPad));
        const ImVec2 modalSize{modalW, modalH};
        const ImVec2 modalPos{
            vp->WorkPos.x + (vp->WorkSize.x - modalW) * 0.5F,
            vp->WorkPos.y + (vp->WorkSize.y - modalH) * 0.5F
        };

        DrawNewProjectBackdrop(vp, modalPos, modalSize);

        ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, WizardLayout::ModalRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg1());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());

        constexpr ImGuiWindowFlags modalFlags = ImGuiWindowFlags_NoTitleBar |
                                                ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoScrollbar |
                                                ImGuiWindowFlags_NoScrollWithMouse;

        ProjectCreationScreenGuiCommand command = ProjectCreationScreenGuiCommand::None;
        ImGui::Begin("ProjectCreationScreen", nullptr, modalFlags);

        DrawWizardHeader(controller, state, fonts, logo, command);

        const float bodyH = ImGui::GetWindowHeight() - WizardLayout::HeaderH - WizardLayout::FooterH;

        DrawWizardSidebar(state, fonts, bodyH);
        ImGui::SameLine(0.0F, 0.0F);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{WizardLayout::MainPadX, WizardLayout::MainPadY});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
        ImGui::BeginChild("WizMain", {0.0F, bodyH}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

        if (state.confirmingDiscard)
        {
            ScopedCard confirmCard("DiscardConfirm", {0.0F, 74.0F}, 16.0F, 12.0F, Theme::ErrSoft());
            {
                ScopedTextStyle ts(fonts.sans, 14.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
                ImGui::TextUnformatted("Unsaved project draft: discard changes and return?");
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0.0F, 6.0F});

            if (Ui::Button({"Keep Editing", {110.0F, 28.0F}, Ui::ButtonVariant::Secondary, true, 13.0F, fonts.mono, Theme::FontPx::Mono}))
            {
                state.confirmingDiscard = false;
            }
            ImGui::SameLine(0.0F, 8.0F);
            if (Ui::Button({"Discard & Return", {140.0F, 28.0F}, Ui::ButtonVariant::Primary, true, 13.0F, fonts.mono, Theme::FontPx::Mono}))
            {
                controller.DiscardDraft();
                command = ProjectCreationScreenGuiCommand::ReturnToWelcome;
                state.confirmingDiscard = false;
            }
            ImGui::Dummy({0.0F, 12.0F});
        }

        ImGui::Dummy({0.0F, 14.0F});

        const ProjectCreationValidation validation = controller.Validate();
        switch (state.step)
        {
        case 1:
            DrawStepTemplate(controller, state, fonts);
            break;
        case 2:
            DrawStepIdentity(controller, state, fonts, validation);
            break;
        case 3:
            DrawStepSettings(controller, state, fonts);
            break;
        case 4:
            DrawStepReview(controller, validation, fonts, state);
            break;
        default:
            state.step = 1;
            DrawStepTemplate(controller, state, fonts);
            break;
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawWizardFooter(controller, state, fonts, validation, command);

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        return command;
    }

} // namespace Horo::Editor
