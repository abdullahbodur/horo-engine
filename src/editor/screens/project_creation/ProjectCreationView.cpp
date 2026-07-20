#include "editor/screens/project_creation/ProjectCreationView.h"
#include <Horo/Editor/Localization/ILocalizationService.h>

#include "Horo/Application/ProjectCompatibility.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Foundation/Logging/Logger.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
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

        [[nodiscard]] std::string GetTemplateName(const int index, const EditorGuiContext& ctx)
        {
            switch (index)
            {
            case 0:
                return ctx.localization.Get("editor", "project_creation.template.empty");
            case 1:
                return ctx.localization.Get("editor", "project_creation.template.3d_starter");
            case 2:
                return ctx.localization.Get("editor", "project_creation.template.first_person");
            case 3:
                return ctx.localization.Get("editor", "project_creation.template.package_based");
            case 4:
                return ctx.localization.Get("editor", "project_creation.template.tech_demo");
            case 5:
                return ctx.localization.Get("editor", "project_creation.template.custom");
            default:
                return "Unknown";
            }
        }

        constexpr std::array kTemplateIds = {
            "empty", "3d-starter", "first-person",
            "package-based", "tech-demo", "custom"
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
            constexpr float CardPad = 18.0F;
            constexpr float CardGap = 18.0F;
            constexpr float CheckGap = 12.0F;

            constexpr float Radius = 4.0F;
            constexpr float TemplateRadius = 6.0F;
            constexpr float ModalRadius = 8.0F;
        } // namespace WizardLayout

        // CopyDraftText was removed — callers now write into std::string directly.

        [[nodiscard]] int FindTemplateIndex(std::string_view templateId)
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

        void SynchronizePresentation(const ProjectCreationController& controller, ProjectCreationViewState& state,
                                     const RendererAvailabilitySnapshot& rendererAvailability)
        {
            if (state.initialized)
            {
                return;
            }
            const ProjectCreationDraft& draft = controller.Draft();
            state.projectName = draft.projectName;
            state.projectPath = draft.projectPath;
            state.projectVersion = draft.projectVersion;
            state.defaultScene = draft.defaultScene;
            state.targetFps = std::to_string(draft.targetFrameRate);

            state.renderBackendIndex = 0;
            for (std::size_t index = 0; index < rendererAvailability.Entries().size(); ++index)
            {
                if (rendererAvailability.Entries()[index].backendId == draft.renderBackend)
                {
                    state.renderBackendIndex = static_cast<int>(index);
                    break;
                }
            }

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

        [[nodiscard]] bool DrawFolderIconButton(const char* id, const float width, const EditorGuiContext& /*ctx*/,
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

            auto* dl = ImGui::GetWindowDrawList();
            const ImVec4 bgColor = active ? Theme::AccentSoft() : Theme::Bg3();
            const ImVec4 hoverBgColor = hovered ? Theme::Hover() : bgColor;
            const ImU32 bgCol = Theme::U32(hoverBgColor);
            const bool hasInteraction = active || hovered;
            const ImVec4 borderColor = error ? Theme::Err() : Theme::Border();
            const ImVec4 interactionBorderColor = hasInteraction ? Theme::Accent() : borderColor;
            const ImU32 borderCol = Theme::U32(interactionBorderColor);
            constexpr float rounding = WizardLayout::Radius;

            dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, bgCol, rounding);
            dl->AddRect(pos, {pos.x + size.x, pos.y + size.y}, borderCol, rounding, 0,
                        (active || hovered) ? 1.5F : 1.0F);

            constexpr float iconW = 18.0F;
            constexpr float iconH = 14.0F;
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

        [[nodiscard]] std::optional<std::filesystem::path> OpenFolderSelectionDialog(const char* prompt)
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
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe)
                return std::nullopt;
            std::string buffer(1024, '\0');
            std::string result;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                result += buffer.c_str();
            }
            if (const int status = pclose(pipe); status != 0 || result.empty())
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
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe)
                return std::nullopt;
            std::string buffer(1024, '\0');
            std::string result;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                result += buffer.c_str();
            }
            if (const int status = pclose(pipe); status != 0 || result.empty())
                return std::nullopt;
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            {
                result.pop_back();
            }
            if (result.empty())
                return std::nullopt;
            return std::filesystem::path(result);
#elif defined(_WIN32)
            std::string cmd =
                "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $f = New-Object "
                "System.Windows.Forms.FolderBrowserDialog; $f.Description = '";
            for (char c : std::string_view(prompt ? prompt : "Select Folder"))
            {
                if (c == '\'' || c == '"')
                    cmd += '`';
                cmd += c;
            }
            cmd += "'; if($f.ShowDialog() -eq 'OK'){ $f.SelectedPath }\" 2>nul";
            FILE* pipe = _popen(cmd.c_str(), "r");
            if (!pipe)
                return std::nullopt;
            std::string buffer(1024, '\0');
            std::string result;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                result += buffer.c_str();
            }
            if (const int status = _pclose(pipe); status != 0 || result.empty())
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

        // DrawInputField (char* overload) was removed — unused.

        struct DrawInputFieldOptions
        {
            const char* hint = nullptr;
            bool error = false;
            const char* errorText = nullptr;
            const char* inputId = "##value";
        };

        bool DrawInputField(const char* label, std::string& value, const size_t maxSize, const float width,
                            const EditorGuiContext& ctx, const DrawInputFieldOptions& opts = {})
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();
            Ui::FieldLabel(label, ctx.theme.fonts);
            const bool changed = Ui::InputTextControl(opts.inputId, value, maxSize, ctx.theme.fonts, opts.error, width);
            if (opts.error && opts.errorText)
                Ui::ErrorText(opts.errorText, ctx.theme.fonts);
            else if (opts.hint)
                Ui::Hint(opts.hint, ctx.theme.fonts);
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        bool DrawComboField(const char* label, int* value, const char* const items[], const int itemCount,
                            const float width,
                            const EditorGuiContext& ctx, const char* hint = nullptr)
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();
            Ui::FieldLabel(label, ctx.theme.fonts);
            if (width != 0.0F)
            {
                ImGui::PushItemWidth(width);
            }
            bool changed = Ui::ComboControl("##value", value, items, itemCount, ctx.theme.fonts);
            if (width != 0.0F)
            {
                ImGui::PopItemWidth();
            }
            if (hint)
            {
                Ui::Hint(hint, ctx.theme.fonts);
            }
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] const RendererBackendAvailability& RendererEntry(const void* context, const int index)
        {
            const auto& availability = *static_cast<const RendererAvailabilitySnapshot*>(context);
            return availability.Entries()[static_cast<std::size_t>(index)];
        }

        [[nodiscard]] const char* RendererEntryLabel(const void* context, const int index)
        {
            return RendererEntry(context, index).displayName.c_str();
        }

        [[nodiscard]] bool RendererEntryEnabled(const void* context, const int index)
        {
            return RendererEntry(context, index).IsSelectable();
        }

        [[nodiscard]] const char* RendererEntryDisabledTooltip(const void* context, const int index)
        {
            const RendererBackendAvailability& entry = RendererEntry(context, index);
            return entry.diagnostic.empty() ? "Renderer is unavailable." : entry.diagnostic.c_str();
        }

        bool DrawRendererBackendField(int& value, const RendererAvailabilitySnapshot& availability, const float width,
                                      const EditorGuiContext& ctx)
        {
            const auto& entries = availability.Entries();
            if (entries.empty())
            {
                return false;
            }
            value = std::clamp(value, 0, static_cast<int>(entries.size()) - 1);

            ImGui::PushID("RENDER BACKEND");
            ImGui::BeginGroup();
            Ui::FieldLabel("RENDER BACKEND", ctx.theme.fonts);
            ImGui::PushItemWidth(width);
            const Ui::ComboItemSource source{
                .context = &availability,
                .label = RendererEntryLabel,
                .enabled = RendererEntryEnabled,
                .disabledTooltip = RendererEntryDisabledTooltip
            };
            const bool changed = Ui::ComboControl("##value", &value, static_cast<int>(entries.size()), source,
                                                  ctx.theme.fonts);
            ImGui::PopItemWidth();
            const RendererBackendAvailability& selected = entries[static_cast<std::size_t>(value)];
            Ui::Hint(selected.IsSelectable()
                         ? "Available on this editor installation."
                         : (selected.diagnostic.empty() ? "Renderer is unavailable." : selected.diagnostic.c_str()),
                     ctx.theme.fonts);
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        void CheckboxCss(const char* label, bool* value, const EditorGuiContext& ctx)
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
                ScopedTextStyle ts(ctx.theme.fonts.sans, 13.0F, Theme::FontPx::Sans);
                ImGui::Checkbox(label, value);
            }
            ImGui::PopStyleColor(6);
            ImGui::PopStyleVar(4);
        }

        void DrawNewProjectBackdrop(const GuiContentRegion& contentRegion, const ImVec2 modalPos,
                                    const ImVec2 modalSize)
        {
            auto* dl = ImGui::GetBackgroundDrawList();

            dl->AddRectFilled(ImVec2{contentRegion.x, contentRegion.y},
                              {contentRegion.x + contentRegion.width, contentRegion.y + contentRegion.height},
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
                                  Theme::U32(col), WizardLayout::ModalRadius + spread);
            }
        }

        void DrawWizardHeader(const ProjectCreationController& controller, ProjectCreationViewState& st,
                              const EditorGuiContext& ctx, const ImTextureID logo,
                              ProjectCreationViewCommand& outCommand)
        {
            using namespace Theme;
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
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
                ScopedTextStyle ts(ctx.theme.fonts.sansEmphasis, 14.0F, FontPx::SansEmphasis);
                ImGui::PushStyleColor(ImGuiCol_Text, Text());
                const std::string title = ctx.localization.Get("editor", "project_creation.title");
                ImGui::TextUnformatted(title.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorPos({HeaderPadX, 36.0F});
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 12.0F, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                const std::string subtitle = ctx.localization.Get("editor", "project_creation.subtitle");
                ImGui::TextUnformatted(subtitle.c_str());
                ImGui::PopStyleColor();
            }

            constexpr ImVec2 closeSize{38.0F, 36.0F};
            ImGui::SetCursorPos({headerW - HeaderPadX - closeSize.x, 11.0F});
            if (Ui::IconCloseButton("close-new-project", closeSize))
            {
                if (controller.LeaveIntent() == ProjectCreationLeaveIntent::RequireDiscardConfirmation)
                {
                    st.confirmingDiscard = true;
                }
                else
                {
                    outCommand = ProjectCreationViewCommand::ReturnToWelcome;
                }
            }

            auto* dl = ImGui::GetWindowDrawList();
            dl->AddLine({headerPos.x, headerPos.y + HeaderH - 1.0F},
                        {headerPos.x + headerW, headerPos.y + HeaderH - 1.0F},
                        U32(Border()), 1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawWizardSidebar(ProjectCreationViewState& st, const EditorGuiContext& ctx, const float sideH)
        {
            using namespace Theme;
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
            ImGui::BeginChild("WizSide", {SidebarW, sideH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 sidePos = ImGui::GetWindowPos();
            auto* dl = ImGui::GetWindowDrawList();

            const std::array<std::string, 4> stepLabelsStr = {
                ctx.localization.Get("editor", "project_creation.step.template.title"),
                ctx.localization.Get("editor", "project_creation.step.identity.title"),
                ctx.localization.Get("editor", "project_creation.step.settings.title"),
                ctx.localization.Get("editor", "project_creation.step.review.title")
            };
            const std::array<const char*, 4> kStepLabels = {
                stepLabelsStr[0].c_str(), stepLabelsStr[1].c_str(),
                stepLabelsStr[2].c_str(), stepLabelsStr[3].c_str()
            };

            const std::array<std::string, 4> stepDescsStr = {
                ctx.localization.Get("editor", "project_creation.step.template.desc"),
                ctx.localization.Get("editor", "project_creation.step.identity.desc"),
                ctx.localization.Get("editor", "project_creation.step.settings.desc"),
                ctx.localization.Get("editor", "project_creation.step.review.desc")
            };
            const std::array<const char*, 4> kStepDescs = {
                stepDescsStr[0].c_str(), stepDescsStr[1].c_str(),
                stepDescsStr[2].c_str(), stepDescsStr[3].c_str()
            };

            ImGui::SetCursorPos({SidebarPadX, SidebarPadY});

            for (int s = 1; s <= 4; ++s)
            {
                ImGui::PushID(s);
                const bool active = (st.step == s);
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                constexpr ImVec2 rowSize{SidebarW - SidebarPadX * 2.0F, StepH};

                if (active)
                {
                    dl->AddRectFilled(rowMin, {rowMin.x + rowSize.x, rowMin.y + rowSize.y},
                                      U32(AccentSoft()),
                                      Radius);
                }

                ImGui::InvisibleButton("##step", rowSize);
                if (ImGui::IsItemClicked())
                {
                    st.step = s;
                }

                const ImVec2 circleCenter{rowMin.x + 10.0F + 11.0F, rowMin.y + 11.0F + 11.0F};
                dl->AddCircleFilled(circleCenter, 11.0F, U32(active ? Accent() : Bg3()), 24);
                dl->AddCircle(circleCenter, 11.0F, U32(active ? Accent() : Border()), 24, 1.0F);

                static constexpr std::array<const char*, 5> kStepNumbers = {"", "1", "2", "3", "4"};
                const char* number = kStepNumbers[s];
                ImFont* numberFont = ctx.theme.fonts.sansCompact ? ctx.theme.fonts.sansCompact : ImGui::GetFont();
                constexpr float numberFontSize = 13.0F;
                const ImVec2 numberSize = numberFont->CalcTextSizeA(numberFontSize, FLT_MAX, 0.0F, number);
                dl->AddText(numberFont, numberFontSize,
                            {circleCenter.x - numberSize.x * 0.5F, circleCenter.y - numberSize.y * 0.5F},
                            U32(active ? DarkText() : Dim()), number);

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 7.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 17.0F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? Text() : Muted());
                    ImGui::TextUnformatted(kStepLabels[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 34.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 14.0F, FontPx::SansCompact);
                    ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                    ImGui::TextUnformatted(kStepDescs[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x, rowMin.y + StepH + StepGap});
                ImGui::PopID();
            }

            dl->AddLine({sidePos.x + SidebarW - 1.0F, sidePos.y}, {sidePos.x + SidebarW - 1.0F, sidePos.y + sideH},
                        U32(Border()), 1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawTemplateIcon(ImDrawList* drawList, const int index, const ImVec2 iconPos, const ImU32 iconColor)
        {
            if (index == 0)
            {
                constexpr float size = 18.0F;
                constexpr float offset = (WizardLayout::TemplateIconPx - size) * 0.5F;
                drawList->AddRect({iconPos.x + offset, iconPos.y + offset},
                                  {iconPos.x + offset + size, iconPos.y + offset + size}, iconColor, 3.0F, 0, 2.0F);
                return;
            }
            if (index == 1)
            {
                const float centerX = iconPos.x + 12.0F;
                const float centerY = iconPos.y + 12.0F;
                drawList->AddRect({iconPos.x + 4.0F, iconPos.y + 6.0F}, {iconPos.x + 20.0F, iconPos.y + 18.0F},
                                  iconColor, 2.0F,
                                  0, 1.5F);
                drawList->AddLine({centerX, iconPos.y + 6.0F}, {centerX, iconPos.y + 18.0F}, iconColor, 1.5F);
                drawList->AddLine({iconPos.x + 4.0F, centerY}, {iconPos.x + 20.0F, centerY}, iconColor, 1.5F);
                return;
            }
            if (index == 2)
            {
                const float centerX = iconPos.x + 12.0F;
                drawList->AddCircle({centerX, iconPos.y + 6.0F}, 3.5F, iconColor, 12, 1.5F);
                drawList->AddRect({centerX - 7.0F, iconPos.y + 12.0F}, {centerX + 7.0F, iconPos.y + 20.0F}, iconColor,
                                  4.0F, 0,
                                  1.5F);
                return;
            }
            if (index == 3)
            {
                drawList->AddRect({iconPos.x + 4.0F, iconPos.y + 6.0F}, {iconPos.x + 20.0F, iconPos.y + 18.0F},
                                  iconColor, 2.0F,
                                  0, 1.5F);
                drawList->AddLine({iconPos.x + 4.0F, iconPos.y + 10.0F}, {iconPos.x + 20.0F, iconPos.y + 10.0F},
                                  iconColor,
                                  1.5F);
                drawList->AddLine({iconPos.x + 12.0F, iconPos.y + 10.0F}, {iconPos.x + 12.0F, iconPos.y + 18.0F},
                                  iconColor,
                                  1.5F);
                return;
            }
            if (index == 4)
            {
                drawList->AddRect({iconPos.x + 3.0F, iconPos.y + 5.0F}, {iconPos.x + 21.0F, iconPos.y + 17.0F},
                                  iconColor, 2.0F,
                                  0, 1.5F);
                drawList->AddLine({iconPos.x + 8.0F, iconPos.y + 20.0F}, {iconPos.x + 16.0F, iconPos.y + 20.0F},
                                  iconColor,
                                  1.5F);
                drawList->AddLine({iconPos.x + 12.0F, iconPos.y + 17.0F}, {iconPos.x + 12.0F, iconPos.y + 20.0F},
                                  iconColor,
                                  1.5F);
                return;
            }
            drawList->AddLine({iconPos.x + 4.0F, iconPos.y + 7.0F}, {iconPos.x + 20.0F, iconPos.y + 7.0F}, iconColor,
                              1.5F);
            drawList->AddCircleFilled({iconPos.x + 9.0F, iconPos.y + 7.0F}, 2.0F, iconColor);
            drawList->AddLine({iconPos.x + 4.0F, iconPos.y + 12.0F}, {iconPos.x + 20.0F, iconPos.y + 12.0F}, iconColor,
                              1.5F);
            drawList->AddCircleFilled({iconPos.x + 15.0F, iconPos.y + 12.0F}, 2.0F, iconColor);
            drawList->AddLine({iconPos.x + 4.0F, iconPos.y + 17.0F}, {iconPos.x + 20.0F, iconPos.y + 17.0F}, iconColor,
                              1.5F);
            drawList->AddCircleFilled({iconPos.x + 11.0F, iconPos.y + 17.0F}, 2.0F, iconColor);
        }

        void DrawTemplateCardContent(const EditorGuiContext& ctx, const int index, const bool selected,
                                     const float cardW,
                                     const char* desc)
        {
            using namespace Theme;
            using namespace WizardLayout;

            const ImVec2 iconPos = ImGui::GetCursorScreenPos();
            auto* drawList = ImGui::GetWindowDrawList();
            const ImU32 iconColor = U32(selected ? Accent() : Text());

            DrawTemplateIcon(drawList, index, iconPos, iconColor);

            ImGui::Dummy({TemplateIconPx, TemplateIconPx});
            ImGui::Dummy({0.0F, 8.0F});

            {
                ImFont* nameFont = ctx.theme.fonts.sans ? ctx.theme.fonts.sans : ImGui::GetFont();
                const ImVec2 namePos = ImGui::GetCursorScreenPos();
                const std::string templateName = GetTemplateName(index, ctx);
                const char* name = templateName.c_str();
                ImGui::GetWindowDrawList()->AddText(nameFont, TemplateNamePx, namePos, U32(Text()), name);
                const ImVec2 nameSize = nameFont->CalcTextSizeA(TemplateNamePx, FLT_MAX, 0.0F, name);
                ImGui::Dummy({nameSize.x, nameSize.y});
            }

            ImGui::Dummy({0.0F, 4.0F});

            {
                ImFont* descFont = ctx.theme.fonts.sansCompact ? ctx.theme.fonts.sansCompact : ImGui::GetFont();
                const ImVec2 descPos = ImGui::GetCursorScreenPos();
                const float wrapW = cardW - TemplatePad * 2.0F;
                ImGui::GetWindowDrawList()->AddText(descFont, TemplateDescPx, descPos, U32(Muted()), desc,
                                                    nullptr, wrapW);
                const ImVec2 descSize = descFont->CalcTextSizeA(TemplateDescPx, FLT_MAX, wrapW, desc);
                ImGui::Dummy({wrapW, descSize.y});
            }
        }

        void DrawTemplateCardBorder(const bool selected, const bool hovered)
        {
            using namespace WizardLayout;

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect({min.x - (selected ? 1.0F : 0.0F), min.y - (selected ? 1.0F : 0.0F)},
                                                {max.x + (selected ? 1.0F : 0.0F), max.y + (selected ? 1.0F : 0.0F)},
                                                Theme::U32(hovered && !selected
                                                               ? Theme::BorderStrong()
                                                               : Theme::Accent()),
                                                TemplateRadius, 0, selected ? 1.5F : 1.0F);
        }

        void DrawTemplateCard(ProjectCreationController& controller, const EditorGuiContext& ctx, const int index,
                              const int currentTemplateIndex, const float cardW, const char* desc)
        {
            using namespace WizardLayout;

            ImGui::PushID(index);
            const bool selected = (currentTemplateIndex == index);

            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, TemplateRadius);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{TemplatePad, TemplatePad});
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? Theme::AccentSoft() : Theme::Bg2());
            ImGui::PushStyleColor(ImGuiCol_Border, selected ? Theme::Accent() : Theme::Border());

            ImGui::BeginChild("TemplateCard", {cardW, TemplateH}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoInputs);
            DrawTemplateCardContent(ctx, index, selected, cardW, desc);
            ImGui::EndChild();

            const ImVec2 cardMin = ImGui::GetItemRectMin();
            const ImVec2 cardMax = ImGui::GetItemRectMax();
            const ImVec2 cursorAfterCard = ImGui::GetCursorScreenPos();
            const std::string cardActionId = std::string{"###project_template_"} + kTemplateIds[index];
            ImGui::SetCursorScreenPos(cardMin);
            ImGui::InvisibleButton(cardActionId.c_str(), {cardMax.x - cardMin.x, cardMax.y - cardMin.y});
            ImGui::SetCursorScreenPos(cursorAfterCard);

            if (const bool hovered = ImGui::IsItemHovered(); hovered || selected)
                DrawTemplateCardBorder(selected, hovered);

            if (ImGui::IsItemClicked())
                controller.SetTemplateId(kTemplateIds[index]);

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            ImGui::PopID();
        }

        void DrawStepTemplate(ProjectCreationController& controller, [[maybe_unused]] ProjectCreationViewState& st,
                              const EditorGuiContext& ctx)
        {
            using namespace WizardLayout;

            const std::array<std::string, 6> descsStr = {
                ctx.localization.Get("editor", "project_creation.template.empty.desc"),
                ctx.localization.Get("editor", "project_creation.template.3d_starter.desc"),
                ctx.localization.Get("editor", "project_creation.template.first_person.desc"),
                ctx.localization.Get("editor", "project_creation.template.package_based.desc"),
                ctx.localization.Get("editor", "project_creation.template.tech_demo.desc"),
                ctx.localization.Get("editor", "project_creation.template.custom.desc")
            };
            const std::array<const char*, 6> kDescs = {
                descsStr[0].c_str(), descsStr[1].c_str(), descsStr[2].c_str(),
                descsStr[3].c_str(), descsStr[4].c_str(), descsStr[5].c_str()
            };

            const std::string chooseTitle = ctx.localization.Get("editor", "project_creation.step.template.choose");
            Ui::SectionTitle(chooseTitle.c_str(), ctx.theme.fonts);
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

                DrawTemplateCard(controller, ctx, i, currentTemplateIndex, cardW, kDescs[i]);
            }
        }

        const ProjectCreationDiagnostic* FindDiagnostic(const ProjectCreationValidation& validation,
                                                        const ProjectCreationDiagnosticCode code)
        {
            for (const auto& diag : validation.diagnostics)
            {
                if (diag.code == code)
                    return &diag;
            }
            return nullptr;
        }

        void DrawProjectLocationField(ProjectCreationController& controller, ProjectCreationViewState& st,
                                      const EditorGuiContext& ctx, Input::InputRouter& inputRouter,
                                      const ProjectCreationDiagnostic* pathErr)
        {
            const std::string locationLabel = ctx.localization.Get("editor", "project_creation.identity.location");
            ImGui::PushID(locationLabel.c_str());
            ImGui::BeginGroup();
            Ui::FieldLabel(locationLabel.c_str(), ctx.theme.fonts);
            constexpr float buttonWidth = 38.0F;
            constexpr float gapWidth = 8.0F;
            const float inputWidth = ImGui::GetContentRegionAvail().x - (buttonWidth + gapWidth);
            (void)Ui::InputTextControl("###project_creation_location", st.projectPath, 512, ctx.theme.fonts,
                                       pathErr != nullptr, std::max(inputWidth, 1.0F));
            controller.SetProjectPath(st.projectPath);
            ImGui::SameLine(0.0F, gapWidth);
            const std::string selectPrompt = ctx.localization.
                                                 Get("editor", "project_creation.identity.location.select");
            if (DrawFolderIconButton("##browse_location", buttonWidth, ctx, pathErr != nullptr))
            {
                auto nativeDialogContext = inputRouter.PushContext(
                    Input::InputContextId{"editor.native_dialog.project_location"},
                    Input::InputContextKind::NativeDialog);
                if (const auto selectedDir = OpenFolderSelectionDialog(selectPrompt.c_str()))
                {
                    std::filesystem::path finalPath = *selectedDir;
                    if (!st.projectName.empty() && finalPath.filename().string() != st.projectName)
                        finalPath /= st.projectName;
                    const std::string pathString = finalPath.string();
                    st.projectPath = pathString.substr(0, 511);
                    controller.SetProjectPath(st.projectPath);
                }
            }
            if (pathErr)
                Ui::ErrorText(pathErr->message.c_str(), ctx.theme.fonts);
            else
            {
                const std::string locationHint = ctx.localization.Get(
                    "editor", "project_creation.identity.location.hint");
                Ui::Hint(locationHint.c_str(), ctx.theme.fonts);
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }

        void DrawStepIdentity(ProjectCreationController& controller, ProjectCreationViewState& st,
                              const EditorGuiContext& ctx,
                              Input::InputRouter& inputRouter,
                              const ProjectCreationValidation& validation)
        {
            using namespace WizardLayout;

            const auto* nameErrRequired =
                FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectNameRequired);
            const auto* nameErrSep =
                FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectNameContainsPathSeparator);
            const auto* nameErr = nameErrRequired ? nameErrRequired : nameErrSep;

            const auto* pathErrReq = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathRequired);
            const auto* pathErrOcc = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathOccupied);
            const auto* pathErrDir = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathNotDirectory);
            const auto* pathErrAcc = FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectPathInaccessible);
            const auto* pathErrWrt =
                FindDiagnostic(validation, ProjectCreationDiagnosticCode::ProjectParentNotWritable);
            const auto* pathErrFirst = pathErrReq ? pathErrReq : pathErrOcc;
            const auto* pathErrSecond = pathErrDir ? pathErrDir : pathErrAcc;
            const auto* pathErr = pathErrFirst ? pathErrFirst : pathErrSecond;
            if (pathErr == nullptr)
                pathErr = pathErrWrt;

            const std::string identityTitle = ctx.localization.Get("editor", "project_creation.step.identity.title");
            Ui::SectionTitle(identityTitle.c_str(), ctx.theme.fonts);
            ImGui::Dummy({0.0F, 14.0F});

            const std::string nameLabel = ctx.localization.Get("editor", "project_creation.identity.name");
            const std::string nameHint = ctx.localization.Get("editor", "project_creation.identity.name.hint");
            DrawInputField(nameLabel.c_str(), st.projectName, 128, -1.0F, ctx,
                           DrawInputFieldOptions{
                               .hint = nameHint.c_str(),
                               .error = nameErr != nullptr,
                               .errorText = nameErr ? nameErr->message.c_str() : nullptr,
                               .inputId = "###project_creation_name"
                           });
            controller.SetProjectName(st.projectName);

            ImGui::Dummy({0.0F, GridGap});

            DrawProjectLocationField(controller, st, ctx, inputRouter, pathErr);

            ImGui::Dummy({0.0F, GridGap});

            const std::string versionLabel = ctx.localization.Get("editor", "project_creation.identity.version");
            const std::string versionHint = ctx.localization.Get("editor", "project_creation.identity.version.hint");
            DrawInputField(versionLabel.c_str(), st.projectVersion, 32, -1.0F, ctx,
                           DrawInputFieldOptions{.hint = versionHint.c_str()});
            controller.SetProjectVersion(st.projectVersion);

            ImGui::Dummy({0.0F, GridGap});

            const std::string sceneLabel = ctx.localization.Get("editor", "project_creation.identity.scene");
            DrawInputField(sceneLabel.c_str(), st.defaultScene, 128, -1.0F, ctx);
            controller.SetDefaultScene(st.defaultScene);

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("DirCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2());
                const std::string dirTitle = ctx.localization.Get("editor", "project_creation.identity.directory");
                Ui::SectionTitle(dirTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 6.0F});
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 13.5F, Theme::FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted("MyGame/\n"
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

        void DrawStepSettings(ProjectCreationController& controller, ProjectCreationViewState& st,
                              const EditorGuiContext& ctx,
                              const RendererAvailabilitySnapshot& rendererAvailability)
        {
            using namespace WizardLayout;
            static constexpr std::array<const char*, 2> kPhysics = {"Enabled", "Disabled"};
            static constexpr std::array<const char*, 3> kBuildProfile = {
                "desktop-debug", "desktop-profile",
                "desktop-release"
            };
            static constexpr std::array<const char*, 3> kAssetCompression = {"lz4", "none", "zstd"};
            static constexpr std::array<const char*, 4> kTextureCompression = {"bc7", "bc5", "astc", "none"};
            static constexpr std::array<const char*, 4> kPlatform = {"host", "windows", "linux", "macos"};
            static constexpr std::array<const char*, 4> kCompiler = {"default", "clang", "gcc", "msvc"};
            static constexpr std::array<const char*, 2> kCppStd = {"C++20", "C++17"};

            const std::string& templateId = controller.Draft().templateId;
            if (templateId == "package-based")
            {
                ScopedCard card("TcPkg", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string pkgTitle = ctx.localization.Get("editor", "project_creation.settings.package");
                Ui::SectionTitle(pkgTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 8.0F});
                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;
                DrawInputField("TEMPLATE PACKAGE URL / REGISTRY", st.packageRegistryUrl, 256, colW, ctx);
                ImGui::SameLine(0.0F, GridGap);
                DrawInputField("PACKAGE VERSION / TAG", st.packageVersion, 64, colW, ctx);
                ImGui::Dummy({0.0F, 8.0F});
                Ui::Hint("Specify the remote registry package and version lockfile to scaffold this project.",
                         ctx.theme.fonts);
            }
            else if (templateId == "first-person")
            {
                ScopedCard card("TcFp", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string fpTitle = ctx.localization.Get("editor", "project_creation.settings.first_person");
                Ui::SectionTitle(fpTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 8.0F});
                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;
                static constexpr std::array<const char*, 3> kInputMaps = {
                    "QWERTY / Mouse", "AZERTY / Mouse",
                    "Gamepad (XInput/SDL)"
                };
                DrawComboField("CHARACTER INPUT MAP", &st.firstPersonInputMapIndex, kInputMaps.data(),
                               kInputMaps.size(), colW, ctx);
                ImGui::Dummy({0.0F, 8.0F});
                Ui::Hint("A first-person camera and kinematic character capsule will be generated in defaultScene.",
                         ctx.theme.fonts);
            }
            else if (templateId == "tech-demo")
            {
                ScopedCard card("TcDemo", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string demoTitle = ctx.localization.Get("editor", "project_creation.settings.tech_demo");
                Ui::SectionTitle(demoTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 8.0F});
                CheckboxCss("Enable runtime observability and FPS overlays by default", &st.demoObservabilityOverlays,
                            ctx);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Include high-detail benchmark scene and camera animation track", &st.demoBenchmarkScene,
                            ctx);
            }
            else if (templateId == "custom")
            {
                ScopedCard card("TcCustom", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string customTitle = ctx.localization.Get("editor", "project_creation.settings.custom");
                Ui::SectionTitle(customTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 8.0F});
                CheckboxCss("Rendering Subsystem (Vulkan/OpenGL core pipeline)", &st.customSubsystems[0], ctx);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Physics Subsystem (Collision, Raycasting, Rigidbodies)", &st.customSubsystems[1], ctx);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Audio Subsystem (3D Spatial Audio & Mixer)", &st.customSubsystems[2], ctx);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("UI Subsystem (ImGui & Retained Scene UI)", &st.customSubsystems[3], ctx);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Networking Subsystem (Replication & Socket Transport)", &st.customSubsystems[4], ctx);
            }

            if (templateId == "package-based" || templateId == "first-person" || templateId == "tech-demo" ||
                templateId == "custom")
            {
                ImGui::Dummy({0.0F, CardGap});
            }

            {
                ScopedCard card("RtCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string runtimeTitle = ctx.localization.Get("editor", "project_creation.settings.runtime");
                Ui::SectionTitle(runtimeTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 8.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                static_cast<void>(DrawRendererBackendField(st.renderBackendIndex, rendererAvailability, colW, ctx));
                if (!rendererAvailability.Entries().empty())
                {
                    controller.SetRenderBackend(
                        rendererAvailability.Entries()[static_cast<std::size_t>(st.renderBackendIndex)].backendId);
                }

                ImGui::SameLine(0.0F, GridGap);
                DrawInputField("TARGET FRAME RATE", st.targetFps, 16, colW, ctx);
                controller.SetTargetFrameRate(std::atoi(st.targetFps.c_str()));

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("PHYSICS", &st.physicsIndex, kPhysics.data(), kPhysics.size(), colW,
                               ctx);
                controller.SetPhysicsEnabled(st.physicsIndex == 0);

                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("BUILD PROFILE", &st.buildProfileIndex, kBuildProfile.data(),
                               kBuildProfile.size(), colW, ctx);
                controller.SetBuildProfile(kBuildProfile[st.buildProfileIndex]);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("ASSET COMPRESSION", &st.assetCompressionIndex, kAssetCompression.data(),
                               kAssetCompression.size(), colW, ctx);
                controller.SetAssetCompression(kAssetCompression[st.assetCompressionIndex]);

                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("TEXTURE COMPRESSION", &st.textureCompressionIndex, kTextureCompression.data(),
                               kTextureCompression.size(), colW, ctx);
                controller.SetTextureCompression(kTextureCompression[st.textureCompressionIndex]);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("TcCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string toolchainTitle = ctx.localization.
                                                       Get("editor", "project_creation.settings.toolchain");
                Ui::SectionTitle(toolchainTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 8.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                DrawComboField("TARGET PLATFORM", &st.targetPlatformIndex, kPlatform.data(),
                               kPlatform.size(),
                               colW, ctx);
                controller.SetTargetPlatform(kPlatform[st.targetPlatformIndex]);

                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("COMPILER FAMILY", &st.compilerFamilyIndex, kCompiler.data(),
                               kCompiler.size(),
                               colW, ctx);
                controller.SetCompilerFamily(kCompiler[st.compilerFamilyIndex]);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("MINIMUM C++ STANDARD", &st.cppStandardIndex, kCppStd.data(),
                               kCppStd.size(),
                               colW, ctx);
                controller.SetMinimumCxxStandard(st.cppStandardIndex == 0 ? 20 : 17);

                ImGui::Dummy({0.0F, 10.0F});
                Ui::Hint("Portable project settings describe build intent. Machine-specific paths and SDK "
                         "locations are resolved by user-level toolchain profiles, never stored in project.json.",
                         ctx.theme.fonts);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("OptCard", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string optTitle = ctx.localization.Get("editor", "project_creation.settings.optional");
                Ui::SectionTitle(optTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 10.0F});

                bool initGit = controller.Draft().initializeGit;
                CheckboxCss("Initialize git repository", &initGit, ctx);
                controller.SetInitializeGit(initGit);

                ImGui::Dummy({0.0F, CheckGap});
                bool restorePkgs = controller.Draft().restorePackages;
                CheckboxCss("Restore packages after creation", &restorePkgs, ctx);
                controller.SetRestorePackages(restorePkgs);

                ImGui::Dummy({0.0F, CheckGap});
                bool inclStarter = controller.Draft().includeStarterContent;
                CheckboxCss("Include starter content", &inclStarter, ctx);
                controller.SetIncludeStarterContent(inclStarter);

                ImGui::Dummy({0.0F, CheckGap});
                bool genCMake = controller.Draft().generateCMakeProject;
                CheckboxCss("Generate CMake project files", &genCMake, ctx);
                controller.SetGenerateCMakeProject(genCMake);
            }
        }

        void SummaryRow(const char* label, std::string_view value, const EditorGuiContext& ctx, const bool warn,
                        const bool last = false)
        {
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            constexpr float rowH = 26.0F;
            constexpr float textYOffset = 4.0F;
            ImGui::SetCursorScreenPos({rowStart.x, rowStart.y + textYOffset});

            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 13.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }

            float valueW = 0.0F;
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 12.0F, Theme::FontPx::SansCompact);
                valueW = ImGui::CalcTextSize(value.data(), value.data() + value.size()).x;
            }

            ImGui::SameLine(std::max(0.0F, rowW - valueW));
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 12.0F, Theme::FontPx::SansCompact);
                if (warn)
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warn());
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted(value.data(), value.data() + value.size());
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({rowStart.x, rowStart.y + rowH});
            if (!last)
            {
                auto* dl = ImGui::GetWindowDrawList();
                constexpr float kDashStep = 7.0F;
                constexpr float kDashLen = 4.0F;
                const auto steps = static_cast<int>(std::ceil(rowW / kDashStep));
                for (int i = 0; i < steps; ++i)
                {
                    const float x0 = rowStart.x + static_cast<float>(i) * kDashStep;
                    const float x1 = std::min(rowStart.x + rowW, x0 + kDashLen);
                    dl->AddLine({x0, rowStart.y + rowH - 1.0F}, {x1, rowStart.y + rowH - 1.0F},
                                Theme::U32(Theme::Border()),
                                1.0F);
                }
            }
        }

        void DrawStepReview(const ProjectCreationController& controller, const ProjectCreationValidation& validation,
                            const EditorGuiContext& ctx, const ProjectCreationViewState& st)
        {
            using namespace WizardLayout;
            const ProjectCreationDraft& draft = controller.Draft();

            {
                ScopedCard card("RevCard1", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string pSettingsTitle = ctx.localization.Get("editor", "project_creation.review.settings");
                Ui::SectionTitle(pSettingsTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 6.0F});

                const int templateIdx = FindTemplateIndex(draft.templateId);
                const std::string tplName = GetTemplateName(templateIdx, ctx);
                SummaryRow("Template", tplName, ctx, false);
                SummaryRow("Project Name", draft.projectName, ctx, false);
                SummaryRow("Project Path", draft.projectPath, ctx, !validation.IsValid());
                SummaryRow("Version", draft.projectVersion, ctx, false);
                SummaryRow("Default Scene", draft.defaultScene, ctx, false);
                SummaryRow("Render Backend", draft.renderBackend, ctx, false);
                SummaryRow("Physics", draft.physicsEnabled ? "Enabled" : "Disabled", ctx, false);
                const bool hasExtraRows = (draft.templateId == "package-based" || draft.templateId == "first-person" ||
                    draft.templateId == "tech-demo" || draft.templateId == "custom");
                SummaryRow("Build Profile", draft.buildProfile, ctx, false, !hasExtraRows);

                if (draft.templateId == "package-based")
                {
                    SummaryRow("Package Registry", st.packageRegistryUrl, ctx, false);
                    SummaryRow("Package Version", st.packageVersion, ctx, false, true);
                }
                else if (draft.templateId == "first-person")
                {
                    static constexpr std::array<const char*, 3> kInputMaps = {
                        "QWERTY / Mouse", "AZERTY / Mouse",
                        "Gamepad (XInput/SDL)"
                    };
                    SummaryRow("Input Map", kInputMaps[st.firstPersonInputMapIndex], ctx, false, true);
                }
                else if (draft.templateId == "tech-demo")
                {
                    SummaryRow("Observability", st.demoObservabilityOverlays ? "Enabled" : "Disabled", ctx, false);
                    SummaryRow("Benchmark Scene", st.demoBenchmarkScene ? "Included" : "Excluded", ctx, false, true);
                }
                else if (draft.templateId == "custom")
                {
                    SummaryRow("Subsystems", "Rendering, Physics, Audio", ctx, false, true);
                }
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("RevCard2", {0.0F, 0.0F}, CardPad, CardPad, Theme::Bg2(), true);
                const std::string createTitle = ctx.localization.Get(
                    "editor", "project_creation.review.what_will_be_created");
                Ui::SectionTitle(createTitle.c_str(), ctx.theme.fonts);
                ImGui::Dummy({0.0F, 6.0F});
                SummaryRow("Portable metadata (commit)", ".horo/project.json, .horo/plugins.json, asset sidecars", ctx,
                           false);
                SummaryRow("Local / derived (ignore)",
                           ".horo/editor workspace.json, .horo/asset index.json, .horo/local/", ctx,
                           false);
                SummaryRow("Build output (ignore)", "build/", ctx, false);
                const std::string projectContract = "Horo " + Application::FormatHoroVersion(
                    Application::CurrentEngineReleaseVersion().value) + " \xC2\xB7 " +
                    ctx.localization.Get("editor", "project_creation.review.project_id_generated");
                const std::string compatibilityLabel =
                    ctx.localization.Get("editor", "project_creation.review.compatibility");
                SummaryRow(compatibilityLabel.c_str(), projectContract, ctx, false);

                std::string validationText = "Ready to create";
                if (!validation.IsValid() && !validation.diagnostics.empty())
                {
                    validationText = "Edit \xE2\x80\x94 " + validation.diagnostics.front().message;
                }
                SummaryRow("Validation mode", validationText, ctx, !validation.IsValid());
                SummaryRow("Recommended .gitignore", ".horo/{editor workspace,asset index}.json .horo/local/ build/",
                           ctx,
                           false, true);
            }
        }

        void DrawWizardFooter(const ProjectCreationController& controller, ProjectCreationViewState& st,
                              const EditorGuiContext& ctx, const ProjectCreationValidation& validation,
                              ProjectCreationViewCommand& outCommand)
        {
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("WizFtr", {0, FooterH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 footerPos = ImGui::GetWindowPos();
            const float footerW = ImGui::GetWindowWidth();
            auto* dl = ImGui::GetWindowDrawList();

            dl->AddLine({footerPos.x, footerPos.y}, {footerPos.x + footerW, footerPos.y}, Theme::U32(Theme::Border()),
                        1.0F);

            const ImVec2 dotCenter{footerPos.x + 22.0F + 4.0F, footerPos.y + 26.0F};
            const bool isValid = validation.IsValid();
            dl->AddCircleFilled(dotCenter, 4.0F, Theme::U32(isValid ? Theme::Ok() : Theme::Err()), 16);

            ImGui::SetCursorPos({38.0F, 18.0F});
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 13.0F, Theme::FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                const int templateIdx = FindTemplateIndex(controller.Draft().templateId);
                const std::string templateName = GetTemplateName(templateIdx, ctx);
                if (validation.diagnostics.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::Text("Template: %s", templateName.c_str());
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
                    ImGui::Text("Template: %s \xC2\xB7 %s", templateName.c_str(),
                                validation.diagnostics.front().message.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor();
            }

            const bool isReview = (st.step == 4);
            constexpr float backW = 80.0F;
            constexpr float nextW = 80.0F;
            constexpr float createW = 130.0F;
            constexpr float btnH = 32.0F;
            constexpr float gap = 8.0F;
            const float actionsW = isReview ? (backW + gap + createW) : (backW + gap + nextW);
            ImGui::SetCursorPos({footerW - 22.0F - actionsW, 10.0F});

            const std::string backLabel =
                ctx.localization.Get("editor", "project_creation.back") + "###project_creation_back";
            const std::string nextLabel =
                ctx.localization.Get("editor", "project_creation.next") + "###project_creation_next";
            const std::string createLabel =
                ctx.localization.Get("editor", "project_creation.create") + "###project_creation_create";
            if (Ui::Button({
                backLabel.c_str(),
                {backW, btnH},
                Ui::ButtonVariant::Secondary,
                st.step > 1,
                13.0F,
                ctx.theme.fonts.sansCompact,
                Theme::FontPx::SansCompact
            }))
            {
                st.step--;
            }

            ImGui::SameLine(0.0F, gap);

            if (!isReview)
            {
                if (Ui::Button({
                    nextLabel.c_str(),
                    {nextW, btnH},
                    Ui::ButtonVariant::Primary,
                    true,
                    13.0F,
                    ctx.theme.fonts.sansCompact,
                    Theme::FontPx::SansCompact
                }))
                {
                    st.step++;
                }
            }
            else
            {
                if (Ui::Button({
                    createLabel.c_str(),
                    {createW, btnH},
                    Ui::ButtonVariant::Primary,
                    true,
                    13.0F,
                    ctx.theme.fonts.sansCompact,
                    Theme::FontPx::SansCompact
                }))
                {
                    if (isValid)
                    {
                        (void)controller.BuildCreationRequest();
                        outCommand = ProjectCreationViewCommand::CreateProject;
                    }
                    else if (!validation.diagnostics.empty())
                    {
                        LOG_ERROR("editor.project_creation", "Cannot create project: %s",
                                  validation.diagnostics.front().message.c_str());
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

    ProjectCreationViewCommand DrawProjectCreationView(ProjectCreationController& controller,
                                                       ProjectCreationViewState& state, const EditorGuiContext& ctx,
                                                       Input::InputRouter& inputRouter,
                                                       const RendererAvailabilitySnapshot& rendererAvailability,
                                                       const GuiContentRegion& contentRegion, const ImTextureID logo)
    {
        SynchronizePresentation(controller, state, rendererAvailability);

        const float modalW =
            std::min(WizardLayout::ModalW, std::max(320.0F, contentRegion.width - WizardLayout::ViewportPad));
        const float modalH =
            std::min(WizardLayout::ModalH, std::max(320.0F, contentRegion.height - WizardLayout::ViewportPad));
        const ImVec2 modalSize{modalW, modalH};
        const ImVec2 modalPos{
            contentRegion.x + (contentRegion.width - modalW) * 0.5F,
            contentRegion.y + (contentRegion.height - modalH) * 0.5F
        };

        DrawNewProjectBackdrop(contentRegion, modalPos, modalSize);

        ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, WizardLayout::ModalRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg1());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());

        constexpr ImGuiWindowFlags modalFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        ProjectCreationViewCommand command = ProjectCreationViewCommand::None;
        ImGui::Begin("ProjectCreationScreen", nullptr, modalFlags);

        DrawWizardHeader(controller, state, ctx, logo, command);

        const float bodyH = ImGui::GetWindowHeight() - WizardLayout::HeaderH - WizardLayout::FooterH;

        DrawWizardSidebar(state, ctx, bodyH);
        ImGui::SameLine(0.0F, 0.0F);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{WizardLayout::MainPadX, WizardLayout::MainPadY});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
        ImGui::BeginChild("WizMain", {0.0F, bodyH}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

        if (state.confirmingDiscard)
        {
            ScopedCard confirmCard("DiscardConfirm", {0.0F, 74.0F}, 16.0F, 12.0F, Theme::ErrSoft());
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 14.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
                ImGui::TextUnformatted("Unsaved project draft: discard changes and return?");
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0.0F, 6.0F});

            if (Ui::Button({
                "Keep Editing",
                {110.0F, 28.0F},
                Ui::ButtonVariant::Secondary,
                true,
                13.0F,
                ctx.theme.fonts.sansCompact,
                Theme::FontPx::SansCompact
            }))
            {
                state.confirmingDiscard = false;
            }
            ImGui::SameLine(0.0F, 8.0F);
            if (Ui::Button({
                "Discard & Return",
                {140.0F, 28.0F},
                Ui::ButtonVariant::Primary,
                true,
                13.0F,
                ctx.theme.fonts.sansCompact,
                Theme::FontPx::SansCompact
            }))
            {
                controller.DiscardDraft();
                command = ProjectCreationViewCommand::ReturnToWelcome;
                state.confirmingDiscard = false;
            }
            ImGui::Dummy({0.0F, 12.0F});
        }

        ImGui::Dummy({0.0F, 14.0F});

        const ProjectCreationValidation validation = controller.Validate();
        switch (state.step)
        {
        case 1:
            DrawStepTemplate(controller, state, ctx);
            break;
        case 2:
            DrawStepIdentity(controller, state, ctx, inputRouter, validation);
            break;
        case 3:
            DrawStepSettings(controller, state, ctx, rendererAvailability);
            break;
        case 4:
            DrawStepReview(controller, validation, ctx, state);
            break;
        default:
            state.step = 1;
            DrawStepTemplate(controller, state, ctx);
            break;
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        DrawWizardFooter(controller, state, ctx, validation, command);

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        return command;
    }
} // namespace Horo::Editor
