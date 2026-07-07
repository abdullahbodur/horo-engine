#include "editor/modals/new_project/NewProjectModal.h"

#include "Horo/Editor/EditorTheme.h"
#include "editor/design_system/components/EditorUiComponents.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <string>
#include <string_view>

namespace Horo::Editor
{
    namespace
    {
        using Theme::Fonts;
        using Theme::ScopedTextStyle;
        using Ui::ScopedCard;

        // ── new project wizard ───────────────────────────────────────────────────
        // See new-project-wizard.html — each step is isolated in its own function.
        // HTML mapping:
        //   .modal 900x680, header 58, body 220px + 1fr, footer 52
        //   .main padding 24px 28px, .steps padding 18px 14px

        constexpr const char *kTemplateNames[] = {
            "Empty", "3D Starter", "First Person", "Package Based", "Tech Demo", "Custom"};

        namespace WizardLayout
        {
            // Values copied from new-project-wizard.html. Keep these local to the
            // wizard so the modal can match the mockup without changing unrelated
            // editor surfaces.
            constexpr float ModalW = 900.0F;
            constexpr float ModalH = 680.0F;
            constexpr float ViewportPad = 56.0F; // body padding:28px on both sides

            constexpr float HeaderH = 58.0F;
            constexpr float FooterH = 52.0F;
            constexpr float SidebarW = 220.0F;

            constexpr float HeaderPadX = 22.0F;
            constexpr float SidebarPadX = 14.0F;
            constexpr float SidebarPadY = 18.0F;
            constexpr float MainPadX = 28.0F;
            constexpr float MainPadY = 24.0F;

            constexpr float StepH = 62.0F; // Slightly taller than the HTML mockup to keep larger step text readable.
            constexpr float StepGap = 6.0F;

            constexpr float TemplateGap = 10.0F;
            constexpr float TemplateH = 116.0F; // Taller than the HTML mockup to keep larger template text readable.
            constexpr float TemplatePad = 14.0F;
            constexpr float TemplateIconPx = 24.0F;
            constexpr float TemplateNamePx = 17.0F;
            constexpr float TemplateDescPx = 14.0F;

            constexpr float GridGap = 14.0F;
            constexpr float FieldLabelGap = 4.0F;
            constexpr float HintGap = 3.0F;
            constexpr float CardPad = 18.0F;
            constexpr float CardGap = 16.0F;
            constexpr float CheckGap = 10.0F;

            constexpr float Radius = 4.0F;
            constexpr float TemplateRadius = 6.0F;
            constexpr float ModalRadius = 8.0F;
        } // namespace WizardLayout

        namespace WizardCss
        {
            // :root from new-project-wizard.html
            [[nodiscard]] inline ImVec4 Rgba(float r, float g, float b, float a = 1.0F)
            {
                return ImVec4{r / 255.0F, g / 255.0F, b / 255.0F, a};
            }

            [[nodiscard]] inline ImVec4 Bg0() { return Rgba(0x0a, 0x0c, 0x0f); }
            [[nodiscard]] inline ImVec4 Bg1() { return Rgba(0x12, 0x15, 0x1a); }
            [[nodiscard]] inline ImVec4 Bg2() { return Rgba(0x18, 0x1c, 0x21); }
            [[nodiscard]] inline ImVec4 Bg3() { return Rgba(0x1f, 0x24, 0x2b); }
            [[nodiscard]] inline ImVec4 Hover() { return Rgba(0x23, 0x28, 0x30); }
            [[nodiscard]] inline ImVec4 Border() { return Rgba(0x2a, 0x2f, 0x37); }
            [[nodiscard]] inline ImVec4 Border2() { return Rgba(0x3a, 0x40, 0x49); }
            [[nodiscard]] inline ImVec4 Text() { return Rgba(0xe8, 0xe4, 0xd9); }
            [[nodiscard]] inline ImVec4 Muted() { return Rgba(0x9a, 0x95, 0x8a); }
            [[nodiscard]] inline ImVec4 Dim() { return Rgba(0x5e, 0x5b, 0x54); }
            [[nodiscard]] inline ImVec4 Accent() { return Rgba(0x04, 0xa5, 0xfc); }
            [[nodiscard]] inline ImVec4 AccentSoft() { return Rgba(0x04, 0xa5, 0xfc, 0.15F); }
            [[nodiscard]] inline ImVec4 AccentHover() { return Rgba(0x04, 0xa5, 0xfc, 0.22F); }
            [[nodiscard]] inline ImVec4 AccentActive() { return Rgba(0x04, 0xa5, 0xfc, 0.30F); }
            [[nodiscard]] inline ImVec4 Ok() { return Rgba(0x5f, 0xb8, 0x8a); }
            [[nodiscard]] inline ImVec4 Warn() { return Rgba(0xe8, 0xa3, 0x3d); }
            [[nodiscard]] inline ImVec4 Err() { return Rgba(0xd4, 0x52, 0x4a); }
            [[nodiscard]] inline ImVec4 ErrSoft() { return Rgba(0xd4, 0x52, 0x4a, 0.12F); }
            [[nodiscard]] inline ImVec4 DarkText() { return Rgba(0x05, 0x13, 0x1c); }
            [[nodiscard]] inline ImVec4 Shadow() { return Rgba(0x00, 0x00, 0x00, 0.55F); }
        } // namespace WizardCss

        // Coarse placeholder for std::filesystem::exists / is_empty.
        [[nodiscard]] bool PathLooksOccupied(const char *path)
        {
            // TODO: replace with a real filesystem check. The HTML mockup always
            // presents the default "DesertRun" path as an occupied/existing folder;
            // this mirrors that behavior for now.
            return std::string_view{path}.find("DesertRun") != std::string_view::npos;
        }

        void DrawCssBorderForLastItem(const ImVec4 &color, float rounding, float thickness = 1.0F, float inflate = 0.0F)
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(
                {min.x - inflate, min.y - inflate},
                {max.x + inflate, max.y + inflate},
                Theme::U32(color),
                rounding,
                0,
                thickness);
        }

        void DrawCssFocusRingForLastItem(const bool error = false)
        {
            if (ImGui::IsItemActive())
            {
                DrawCssBorderForLastItem(error ? WizardCss::ErrSoft() : WizardCss::AccentSoft(),
                                         WizardLayout::Radius + 2.0F,
                                         2.0F,
                                         2.0F);
            }
            else if (ImGui::IsItemHovered())
            {
                DrawCssBorderForLastItem(WizardCss::Border2(), WizardLayout::Radius, 1.0F, 0.0F);
            }
        }

        [[nodiscard]] bool DrawWizardButton(const char *label,
                                            const ImVec2 size,
                                            const bool primary,
                                            const bool enabled,
                                            const Fonts &f)
        {
            // button { padding:8px 14px; border:1px solid var(--bd); border-radius:4px;
            //          background:var(--bg3); color:var(--txt); font:500 12px var(--mono) }
            if (!enabled)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45F);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{14.0F, 8.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WizardLayout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);

            if (primary)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, WizardCss::Accent());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WizardCss::Accent());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, WizardCss::Accent());
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::DarkText());
                ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Accent());
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, WizardCss::Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WizardCss::Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, WizardCss::Bg3());
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());
                ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border());
            }

            bool clicked = false;
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                clicked = ImGui::Button(label, size);
            }

            if (enabled && !primary && ImGui::IsItemHovered())
            {
                DrawCssBorderForLastItem(WizardCss::Border2(), WizardLayout::Radius, 1.0F, 0.0F);
            }

            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
            if (!enabled)
            {
                ImGui::PopStyleVar();
                clicked = false;
            }

            return clicked;
        }

        [[nodiscard]] bool InputTextCss(const char *id,
                                        char *buffer,
                                        const size_t bufferSize,
                                        const Fonts &f,
                                        const bool error = false)
        {
            // input { background:var(--bg3); border:1px solid var(--bd); border-radius:4px;
            //         color:var(--txt); padding:7px 10px; font:12px var(--mono) }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WizardLayout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, error ? WizardCss::ErrSoft() : WizardCss::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, error ? WizardCss::ErrSoft() : WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, error ? WizardCss::ErrSoft() : WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, error ? WizardCss::Err() : WizardCss::Border());
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());

            bool changed = false;
            {
                ScopedTextStyle ts(f.mono, 15.0F, Theme::FontPx::Mono);
                changed = ImGui::InputText(id, buffer, bufferSize);
            }
            DrawCssFocusRingForLastItem(error);

            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
            return changed;
        }

        [[nodiscard]] bool ComboCss(const char *id,
                                    int *value,
                                    const char *const items[],
                                    const int itemCount,
                                    const Fonts &f)
        {
            using namespace WizardLayout;

            ImGui::PushID(id);

            // ── closed field: same CSS contract as input/select ──
            // select { padding:7px 10px; border:1px solid var(--bd); border-radius:4px;
            //          background:var(--bg3); font:12px var(--mono) }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            const float fieldW = ImGui::CalcItemWidth();
            const float fieldH = ImGui::GetFrameHeight();
            ImGui::PopStyleVar();

            const ImVec2 fieldPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##field", ImVec2{fieldW, fieldH});
            const bool fieldHovered = ImGui::IsItemHovered();
            const bool fieldClicked = ImGui::IsItemClicked();

            const std::string popupId = std::string("##popup_") + id;
            const bool popupOpen = ImGui::IsPopupOpen(popupId.c_str());

            auto *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(fieldPos, {fieldPos.x + fieldW, fieldPos.y + fieldH},
                              Theme::U32(fieldHovered ? WizardCss::Hover() : WizardCss::Bg3()), Radius);
            dl->AddRect(fieldPos, {fieldPos.x + fieldW, fieldPos.y + fieldH},
                        Theme::U32(popupOpen ? WizardCss::Accent() : WizardCss::Border()),
                        Radius, 0, popupOpen ? 1.5F : 1.0F);

            // Selected value label.
            {
                ImFont *font = f.mono ? f.mono : ImGui::GetFont();
                const char *label = (*value >= 0 && *value < itemCount) ? items[*value] : "";
                dl->AddText(font, 15.0F,
                            {fieldPos.x + 10.0F, fieldPos.y + (fieldH - 15.0F) * 0.5F},
                            Theme::U32(WizardCss::Text()), label);
            }

            // Right-side arrow — draw a small filled triangle to avoid glyph fallback risk.
            {
                const float cx = fieldPos.x + fieldW - 18.0F;
                const float cy = fieldPos.y + fieldH * 0.5F;
                const ImU32 arrowCol = Theme::U32(fieldHovered ? WizardCss::Text() : WizardCss::Muted());
                dl->AddTriangleFilled({cx - 4.0F, cy - 2.0F}, {cx + 4.0F, cy - 2.0F}, {cx, cy + 3.0F}, arrowCol);
            }

            if (fieldClicked)
                ImGui::OpenPopup(popupId.c_str());

            bool changed = false;

            // ── popup: .dropdown { padding:5px 0; border:1px solid var(--bd2);
            //           border-radius:6px; background:var(--bg2); box-shadow } ──
            ImGui::SetNextWindowPos({fieldPos.x, fieldPos.y + fieldH + 4.0F});
            ImGui::SetNextWindowSize({fieldW, 0.0F});

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 5.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleColor(ImGuiCol_PopupBg, WizardCss::Bg2());
            ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border2());

            if (ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoMove))
            {
                const ImVec2 pMin = ImGui::GetWindowPos();
                const ImVec2 pMax = {pMin.x + ImGui::GetWindowWidth(), pMin.y + ImGui::GetWindowHeight()};

                // Soft shadow: same approach as the modal backdrop — layered,
                // translucent rectangles on GetBackgroundDrawList(), drawn behind
                // the popup. The previous version used one thick AddRect on the
                // foreground draw list, which looked like a heavy black border over
                // the popup and made the list feel visually detached.
                auto *bgdl = ImGui::GetBackgroundDrawList();
                constexpr int shadowLayers = 12;
                for (int i = shadowLayers; i >= 1; --i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(shadowLayers);
                    const float spread = 16.0F * t;
                    const float alpha = 0.45F * (1.0F - t) * 0.11F;
                    bgdl->AddRectFilled({pMin.x - spread, pMin.y + 3.0F - spread * 0.25F},
                                        {pMax.x + spread, pMax.y + 3.0F + spread},
                                        Theme::U32(ImVec4{0.0F, 0.0F, 0.0F, alpha}),
                                        6.0F + spread);
                }

                for (int i = 0; i < itemCount; ++i)
                {
                    ImGui::PushID(i);
                    const bool isSelected = (*value == i);
                    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                    const float rowW = ImGui::GetContentRegionAvail().x;
                    constexpr float rowH = 28.0F;

                    ImGui::InvisibleButton("##row", {rowW, rowH});
                    const bool rowHovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked())
                    {
                        *value = i;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }

                    auto *pdl = ImGui::GetWindowDrawList();
                    if (rowHovered || isSelected)
                    {
                        pdl->AddRectFilled(rowMin, {rowMin.x + rowW, rowMin.y + rowH},
                                           Theme::U32(isSelected ? WizardCss::AccentSoft() : WizardCss::Hover()));
                    }
                    if (isSelected)
                    {
                        pdl->AddRectFilled({rowMin.x, rowMin.y + 4.0F},
                                           {rowMin.x + 2.5F, rowMin.y + rowH - 4.0F},
                                           Theme::U32(WizardCss::Accent()));
                    }

                    ImFont *font = f.mono ? f.mono : ImGui::GetFont();
                    pdl->AddText(font, 15.0F,
                                 {rowMin.x + 14.0F, rowMin.y + (rowH - 15.0F) * 0.5F},
                                 Theme::U32(isSelected ? WizardCss::Text() : WizardCss::Muted()),
                                 items[i]);

                    if (isSelected)
                    {
                        const char *check = "\xE2\x9C\x93";
                        const ImVec2 cs = font->CalcTextSizeA(15.0F, FLT_MAX, 0.0F, check);
                        pdl->AddText(font, 15.0F,
                                     {rowMin.x + rowW - cs.x - 14.0F, rowMin.y + (rowH - 15.0F) * 0.5F},
                                     Theme::U32(WizardCss::Accent()), check);
                    }

                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);

            ImGui::PopID();
            return changed;
        }

        void WizardSectionTitle(const char *upperCaseLabel, const Fonts &f)
        {
            // .section-title { font:700 12px var(--mono); letter-spacing:.8px; color:var(--mut); margin:0 0 14px }
            ScopedTextStyle ts(f.monoSemiBold, 16.0F, Theme::FontPx::MonoSemiBold);
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
            ImGui::TextUnformatted(upperCaseLabel);
            ImGui::PopStyleColor();
        }

        void WizardFieldLabel(const char *upperCaseLabel, const Fonts &f)
        {
            // .field label { font:11px var(--mono); color:var(--mut); margin-bottom:4px; letter-spacing:.5px }
            ScopedTextStyle ts(f.mono, 14.0F, Theme::FontPx::Mono);
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
            ImGui::TextUnformatted(upperCaseLabel);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetStyle().ItemSpacing.y - WizardLayout::FieldLabelGap));
        }

        void WizardHint(const char *text, const Fonts &f)
        {
            // .hint { color:var(--dim); font:10.5px var(--mono); margin-top:3px }
            ScopedTextStyle ts(f.mono, 13.5F, Theme::FontPx::Mono);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetStyle().ItemSpacing.y - WizardLayout::HintGap));
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Dim());
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        void ErrorText(const char *text, const Fonts &f)
        {
            ScopedTextStyle ts(f.mono, 14.0F, Theme::FontPx::Mono);
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Err());
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetStyle().ItemSpacing.y - WizardLayout::HintGap));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        void DrawInputField(const char *label,
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
            WizardFieldLabel(label, f);
            ImGui::PushItemWidth(width);
            [[maybe_unused]] const bool changed = InputTextCss("##value", buffer, bufferSize, f, error);
            ImGui::PopItemWidth();
            if (error && errorText)
            {
                ErrorText(errorText, f);
            }
            else if (hint)
            {
                WizardHint(hint, f);
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }

        void DrawComboField(const char *label,
                            int *value,
                            const char *const items[],
                            const int itemCount,
                            const float width,
                            const Fonts &f,
                            const char *hint = nullptr)
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();
            WizardFieldLabel(label, f);
            ImGui::PushItemWidth(width);
            [[maybe_unused]] const bool changed = ComboCss("##value", value, items, itemCount, f);
            ImGui::PopItemWidth();
            if (hint)
            {
                WizardHint(hint, f);
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }

        void CheckboxCss(const char *label, bool *value, const Fonts &f)
        {
            // .check { display:flex; align-items:center; gap:8px; font:12px var(--sans); color:var(--mut) }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{8.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WizardLayout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, WizardCss::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border());
            ImGui::PushStyleColor(ImGuiCol_CheckMark, WizardCss::Accent());
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
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

            // Remove the decorative radial glow behind the modal.
            // Keep only a subtle dark overlay + the modal shadow so the dialog
            // feels focused without adding extra background styling.
            dl->AddRectFilled(vp->WorkPos,
                              {vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y},
                              IM_COL32(0, 0, 0, 90));

            // box-shadow: 0 28px 80px rgba(0,0,0,.55). ImDrawList has no blur,
            // so approximate with layered rounded rects behind the modal.
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

        // .header { height:58px; padding:0 22px; background:var(--bg0); border-bottom:1px solid var(--bd) }
        [[nodiscard]] bool DrawWizardHeader(NewProjectState &st, const Fonts &f, const ::ImTextureID logo)
        {
            using namespace Theme;
            using namespace WizardLayout;

            bool closeRequested = false;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg0());
            ImGui::BeginChild("WizHdr", {0, HeaderH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 headerPos = ImGui::GetWindowPos();
            const float headerW = ImGui::GetWindowWidth();

            // .title { font:700 14px mono; gap:9px; align-items:center }
            ImGui::SetCursorPos({HeaderPadX, 12.0F});
            if (logo)
            {
                ImGui::Image(logo, {20.0F, 20.0F});
                ImGui::SameLine(0.0F, 9.0F);
            }
            {
                ScopedTextStyle ts(f.monoSemiBold, 14.0F, FontPx::MonoSemiBold);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());
                ImGui::TextUnformatted("NEW PROJECT");
                ImGui::PopStyleColor();
            }

            // .subtitle { font:11px mono; color:var(--dim); margin-top:3px }
            ImGui::SetCursorPos({HeaderPadX, 36.0F});
            {
                ScopedTextStyle ts(f.mono, 12.0F, FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Dim());
                ImGui::TextUnformatted("Create portable .horo metadata and starter content");
                ImGui::PopStyleColor();
            }

            // close button: SVG-style X, dark bg on hover
            const ImVec2 closeSize{38.0F, 36.0F};
            ImGui::SetCursorPos({headerW - HeaderPadX - closeSize.x, 11.0F});
            if (Ui::IconCloseButton("close-new-project", closeSize))
            {
                st.open = false;
                closeRequested = true;
            }

            // header bottom border, without turning the header into a bordered child
            ImGui::GetWindowDrawList()->AddLine(
                {headerPos.x, headerPos.y + HeaderH - 1.0F},
                {headerPos.x + headerW, headerPos.y + HeaderH - 1.0F},
                Theme::U32(WizardCss::Border()),
                1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();

            return closeRequested;
        }

        // .steps { width:220px; background:var(--bg2); border-right:1px solid var(--bd); padding:18px 14px }
        void DrawWizardSidebar(NewProjectState &st, const Fonts &f, const float bodyH)
        {
            using namespace Theme;
            using namespace WizardLayout;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{SidebarPadX, SidebarPadY});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg2());
            ImGui::BeginChild("WizSide", {SidebarW, bodyH}, false,
                              ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_AlwaysUseWindowPadding);

            const ImVec2 sidePos = ImGui::GetWindowPos();
            const float sideH = ImGui::GetWindowHeight();
            auto *dl = ImGui::GetWindowDrawList();

            static constexpr const char *kStepLabels[] = {"Template", "Identity", "Settings", "Review"};
            static constexpr const char *kStepDescs[] = {"Choose starter", "Name & location", "Runtime defaults", "Validate & create"};

            for (int s = 1; s <= 4; ++s)
            {
                ImGui::PushID(s);

                const bool active = (st.step == s);
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const float rowW = ImGui::GetContentRegionAvail().x;
                const ImVec2 rowSize{rowW, StepH};

                if (active)
                {
                    dl->AddRectFilled(rowMin,
                                      {rowMin.x + rowSize.x, rowMin.y + rowSize.y},
                                      Theme::U32(WizardCss::AccentSoft()),
                                      Radius);
                }

                ImGui::InvisibleButton("##step", rowSize);
                if (ImGui::IsItemClicked())
                {
                    st.step = s;
                }

                // .step .n { width:22px; height:22px; border-radius:50%; margin/padding alignment }
                const ImVec2 circleCenter{rowMin.x + 10.0F + 11.0F, rowMin.y + 11.0F + 11.0F};
                dl->AddCircleFilled(circleCenter, 11.0F, Theme::U32(active ? WizardCss::Accent() : WizardCss::Bg3()), 24);
                dl->AddCircle(circleCenter, 11.0F, Theme::U32(active ? WizardCss::Accent() : WizardCss::Border()), 24, 1.0F);

                const char *number = (s == 1) ? "1" : (s == 2) ? "2"
                                                  : (s == 3)   ? "3"
                                                               : "4";
                ImFont *numberFont = f.mono ? f.mono : ImGui::GetFont();
                const float numberFontSize = 13.0F;
                const ImVec2 numberSize = numberFont->CalcTextSizeA(numberFontSize, FLT_MAX, 0.0F, number);
                dl->AddText(numberFont,
                            numberFontSize,
                            {circleCenter.x - numberSize.x * 0.5F, circleCenter.y - numberSize.y * 0.5F},
                            Theme::U32(active ? WizardCss::DarkText() : WizardCss::Dim()),
                            number);

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 7.0F});
                {
                    ScopedTextStyle ts(f.sans, 17.0F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? WizardCss::Text() : WizardCss::Muted());
                    ImGui::TextUnformatted(kStepLabels[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 34.0F});
                {
                    ScopedTextStyle ts(f.mono, 14.0F, FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Dim());
                    ImGui::TextUnformatted(kStepDescs[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x, rowMin.y + StepH + StepGap});
                ImGui::PopID();
            }

            // only right separator, no boxed sidebar border
            dl->AddLine({sidePos.x + SidebarW - 1.0F, sidePos.y},
                        {sidePos.x + SidebarW - 1.0F, sidePos.y + sideH},
                        Theme::U32(WizardCss::Border()),
                        1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // Step 1 — .template-grid { grid-template-columns:repeat(3,1fr); gap:10px }
        void DrawStepTemplate(NewProjectState &st, const Fonts &f)
        {
            using namespace Theme;
            using namespace WizardLayout;

            static constexpr const char *kDescs[] = {
                "No starter scene. Minimal asset tree and project.json.",
                "Scene, camera, directional light, floor, material defaults.",
                "Character controller, input map, capsule, and test level.",
                "Create from a verified template package and lockfile.",
                "Rendering samples, observability overlays, benchmark scene.",
                "Pick systems manually before project generation."};

            WizardSectionTitle("CHOOSE A TEMPLATE", f);
            ImGui::Dummy({0.0F, 14.0F});

            const float cardW = (ImGui::GetContentRegionAvail().x - TemplateGap * 2.0F) / 3.0F;

            for (int i = 0; i < 6; ++i)
            {
                if (i % 3 != 0)
                {
                    ImGui::SameLine(0.0F, TemplateGap);
                }

                ImGui::PushID(i);
                const bool selected = (st.selectedTemplate == i);

                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, TemplateRadius);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{TemplatePad, TemplatePad});
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? WizardCss::AccentSoft() : WizardCss::Bg2());
                ImGui::PushStyleColor(ImGuiCol_Border, selected ? WizardCss::Accent() : WizardCss::Border());

                ImGui::BeginChild("TemplateCard",
                                  {cardW, TemplateH},
                                  true,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_AlwaysUseWindowPadding);

                // HTML: .template-icon { font-size:24px; margin-bottom:8px }
                // Draw the square manually instead of relying on the □ glyph;
                // this avoids fallback '?' boxes when the runtime font atlas is incomplete.
                if (i == 0)
                {
                    const ImVec2 iconPos = ImGui::GetCursorScreenPos();
                    const float squareSize = 18.0F;
                    const float squareOffset = (TemplateIconPx - squareSize) * 0.5F;
                    ImGui::GetWindowDrawList()->AddRect(
                        {iconPos.x + squareOffset, iconPos.y + squareOffset},
                        {iconPos.x + squareOffset + squareSize, iconPos.y + squareOffset + squareSize},
                        Theme::U32(WizardCss::Text()),
                        3.0F,
                        0,
                        2.0F);
                    ImGui::Dummy({TemplateIconPx, TemplateIconPx});
                    ImGui::Dummy({0.0F, 8.0F});
                }
                else
                {
                    // Empty .template-icon div still contributes its 8px bottom margin in the HTML.
                    ImGui::Dummy({0.0F, 8.0F});
                }

                {
                    // HTML: .template-name { font:600 13px var(--sans); margin-bottom:4px }
                    // Draw with ImDrawList::AddText(font, pixel_size, ...) so the card uses
                    // exact CSS-sized text instead of inheriting a smaller window font scale.
                    ImFont *nameFont = f.sans ? f.sans : ImGui::GetFont();
                    const ImVec2 namePos = ImGui::GetCursorScreenPos();
                    const char *name = kTemplateNames[i];
                    ImGui::GetWindowDrawList()->AddText(nameFont, TemplateNamePx, namePos, Theme::U32(WizardCss::Text()), name);
                    const ImVec2 nameSize = nameFont->CalcTextSizeA(TemplateNamePx, FLT_MAX, 0.0F, name);
                    ImGui::Dummy({nameSize.x, nameSize.y});
                }

                ImGui::Dummy({0.0F, 4.0F});

                {
                    // HTML: .template-desc { font:11px var(--mono); color:var(--mut) }
                    ImFont *descFont = f.mono ? f.mono : ImGui::GetFont();
                    const ImVec2 descPos = ImGui::GetCursorScreenPos();
                    const float wrapW = cardW - TemplatePad * 2.0F;
                    const char *desc = kDescs[i];
                    ImGui::GetWindowDrawList()->AddText(descFont, TemplateDescPx, descPos, Theme::U32(WizardCss::Muted()), desc, nullptr, wrapW);
                    const ImVec2 descSize = descFont->CalcTextSizeA(TemplateDescPx, FLT_MAX, wrapW, desc);
                    ImGui::Dummy({wrapW, descSize.y});
                }

                ImGui::EndChild();

                const bool hovered = ImGui::IsItemHovered();
                if (hovered || selected)
                {
                    DrawCssBorderForLastItem(hovered && !selected ? WizardCss::Border2() : WizardCss::Accent(),
                                             TemplateRadius,
                                             selected ? 1.5F : 1.0F,
                                             0.0F);
                }

                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    st.selectedTemplate = i;
                }

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(3);
                ImGui::PopID();

                if (i == 2)
                {
                    ImGui::Dummy({0.0F, TemplateGap});
                }
            }
        }

        // Step 2 — .full-field* + Project Directory card
        void DrawStepIdentity(NewProjectState &st, const Fonts &f, const bool pathOccupied)
        {
            using namespace WizardLayout;

            WizardSectionTitle("PROJECT IDENTITY", f);
            ImGui::Dummy({0.0F, 14.0F});

            // HTML: <div style="display:flex;flex-direction:column;gap:14px">
            DrawInputField("PROJECT NAME",
                           st.name,
                           sizeof(st.name),
                           -1.0F,
                           f,
                           "Stored as project.json name; projectId is generated once.");
            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("PROJECT PATH",
                           st.path,
                           sizeof(st.path),
                           -1.0F,
                           f,
                           nullptr,
                           pathOccupied,
                           "Directory already exists; choose an empty folder or import existing project.");
            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("PROJECT VERSION",
                           st.version,
                           sizeof(st.version),
                           -1.0F,
                           f,
                           "Game/product version. Does not select project-format migrations.");
            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("DEFAULT SCENE", st.defaultScene, sizeof(st.defaultScene), -1.0F, f);
            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("DirCard", {0.0F, 250.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("PROJECT DIRECTORY", f);
                ImGui::Dummy({0.0F, 8.0F});
                ScopedTextStyle ts(f.mono, 14.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
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

        // Step 3 — Runtime Defaults / Required Toolchain / Optional cards
        void DrawStepSettings(NewProjectState &st, const Fonts &f)
        {
            using namespace WizardLayout;
            static constexpr const char *kRenderBackend[] = {"opengl", "vulkan", "auto detect"};
            static constexpr const char *kPhysics[] = {"Enabled", "Disabled"};
            static constexpr const char *kBuildProfile[] = {"desktop-debug", "desktop-profile", "desktop-release"};
            static constexpr const char *kAssetCompression[] = {"lz4", "none", "zstd"};
            static constexpr const char *kTextureCompression[] = {"bc7", "bc5", "astc", "none"};
            static constexpr const char *kPlatform[] = {"host", "windows", "linux", "macos"};
            static constexpr const char *kCompiler[] = {"default", "clang", "gcc", "msvc"};
            static constexpr const char *kCppStd[] = {"C++20", "C++17"};

            {
                ScopedCard card("RtCard", {0.0F, 262.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("RUNTIME DEFAULTS", f);
                ImGui::Dummy({0.0F, 10.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                DrawComboField("RENDER BACKEND", &st.renderBackend, kRenderBackend, 3, colW, f,
                               "Default: opengl. Override per host profile.");
                ImGui::SameLine(0.0F, GridGap);
                DrawInputField("TARGET FRAME RATE", st.targetFps, sizeof(st.targetFps), colW, f);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("PHYSICS", &st.physics, kPhysics, 2, colW, f);
                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("BUILD PROFILE", &st.buildProfile, kBuildProfile, 3, colW, f);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("ASSET COMPRESSION", &st.assetCompression, kAssetCompression, 3, colW, f);
                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("TEXTURE COMPRESSION", &st.textureCompression, kTextureCompression, 4, colW, f);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("TcCard", {0.0F, 212.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("REQUIRED TOOLCHAIN", f);
                ImGui::Dummy({0.0F, 10.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                DrawComboField("TARGET PLATFORM", &st.targetPlatform, kPlatform, 4, colW, f);
                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("COMPILER FAMILY", &st.compilerFamily, kCompiler, 4, colW, f);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("MINIMUM C++ STANDARD", &st.cppStandard, kCppStd, 2, colW, f);

                ImGui::Dummy({0.0F, 12.0F});
                WizardHint("Portable project settings describe build intent. Machine-specific paths and SDK "
                           "locations are resolved by user-level toolchain profiles, never stored in project.json.",
                           f);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("OptCard", {0.0F, 180.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("OPTIONAL", f);
                ImGui::Dummy({0.0F, 14.0F});

                CheckboxCss("Initialize git repository", &st.initGit, f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Restore packages after creation", &st.restorePackages, f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Include starter content", &st.includeStarter, f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Generate CMake project files", &st.generateCMake, f);
            }
        }

        // .summary-row { display:flex; justify-content:space-between; padding:8px 0; border-bottom:1px dashed var(--bd) }
        void SummaryRow(const char *label, const std::string &value, const Fonts &f, const bool warn, const bool last = false)
        {

            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            constexpr float rowH = 26.0F;
            constexpr float textYOffset = 4.0F;

            ImGui::SetCursorScreenPos({rowStart.x, rowStart.y + textYOffset});

            {
                ScopedTextStyle ts(f.sans, 13.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
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
                    ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Warn());
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());
                ImGui::TextUnformatted(value.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({rowStart.x, rowStart.y + rowH});
            if (!last)
            {
                auto *dl = ImGui::GetWindowDrawList();
                const ImU32 col = Theme::U32(WizardCss::Border());
                constexpr float kDashStep = 7.0F;
                constexpr float kDashLen = 4.0F;
                const int steps = static_cast<int>(std::ceil(rowW / kDashStep));
                for (int i = 0; i < steps; ++i)
                {
                    const float x = static_cast<float>(i) * kDashStep;
                    dl->AddLine({rowStart.x + x, rowStart.y + rowH - 1.0F},
                                {rowStart.x + std::min(x + kDashLen, rowW), rowStart.y + rowH - 1.0F},
                                col,
                                1.0F);
                }
            }
        }

        // Step 4 — Review cards
        void DrawStepReview(const NewProjectState &st, const Fonts &f, const bool pathOccupied)
        {
            using namespace WizardLayout;
            static constexpr const char *kBuildProfile[] = {"desktop-debug", "desktop-profile", "desktop-release"};
            static constexpr const char *kRenderBackend[] = {"opengl", "vulkan", "auto detect"};
            static constexpr const char *kPhysics[] = {"Enabled", "Disabled"};

            WizardSectionTitle("REVIEW & CREATE", f);
            ImGui::Dummy({0.0F, 14.0F});

            {
                ScopedCard card("RevCard1", {0.0F, 250.0F}, CardPad, CardPad, WizardCss::Bg2());
                SummaryRow("Template", kTemplateNames[st.selectedTemplate], f, false);
                SummaryRow("Project Name", st.name, f, false);
                SummaryRow("Project Path", st.path, f, pathOccupied);
                SummaryRow("Project Version", st.version, f, false);
                SummaryRow("Default Scene", st.defaultScene, f, false);
                SummaryRow("Render Backend", kRenderBackend[st.renderBackend], f, false);
                SummaryRow("Physics", kPhysics[st.physics], f, false);
                SummaryRow("Build Profile", kBuildProfile[st.buildProfile], f, false, true);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("RevCard2", {0.0F, 232.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("WHAT WILL BE CREATED", f);
                ImGui::Dummy({0.0F, 6.0F});
                SummaryRow("Portable metadata (commit)", ".horo/project.json, .horo/plugins.json, asset sidecars", f, false);
                SummaryRow("Local / derived (ignore)", ".horo/editor workspace.json, .horo/asset index.json, .horo/local/", f, false);
                SummaryRow("Build output (ignore)", "build/", f, false);
                SummaryRow("Project schema", "formatVersion 1 · projectId generated", f, false);
                SummaryRow("Validation mode",
                           pathOccupied ? "Edit — 1 blocking issue (path exists)" : "Ready to create",
                           f,
                           pathOccupied);
                SummaryRow("Recommended .gitignore", ".horo/{editor workspace,asset index}.json .horo/local/ build/", f, false, true);
            }
        }

        // .footer { height:52px; padding:0 22px; background:var(--bg0); border-top:1px solid var(--bd) }
        void DrawWizardFooter(NewProjectState &st, const Fonts &f, const bool pathOccupied)
        {
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg0());
            ImGui::BeginChild("WizFtr", {0, FooterH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 footerPos = ImGui::GetWindowPos();
            const float footerW = ImGui::GetWindowWidth();
            auto *dl = ImGui::GetWindowDrawList();

            dl->AddLine({footerPos.x, footerPos.y},
                        {footerPos.x + footerW, footerPos.y},
                        Theme::U32(WizardCss::Border()),
                        1.0F);

            // HTML status: green dot even when text says validation failed.
            const ImVec2 dotCenter{footerPos.x + 22.0F + 4.0F, footerPos.y + 26.0F};
            dl->AddCircleFilled(dotCenter, 4.0F, Theme::U32(WizardCss::Ok()), 16);

            ImGui::SetCursorPos({38.0F, 18.0F});
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
                if (pathOccupied)
                {
                    ImGui::Text("Template: %s \xC2\xB7 path validation failed", kTemplateNames[st.selectedTemplate]);
                }
                else
                {
                    ImGui::Text("Template: %s", kTemplateNames[st.selectedTemplate]);
                }
                ImGui::PopStyleColor();
            }

            const bool isReview = (st.step == 4);
            const float backW = 80.0F;
            const float nextW = 80.0F;
            const float createW = 120.0F;
            const float btnH = 32.0F;
            const float gap = 8.0F;
            const float actionsW = isReview ? (backW + gap + createW) : (backW + gap + nextW);
            ImGui::SetCursorPos({footerW - 22.0F - actionsW, 10.0F});

            if (DrawWizardButton("\xE2\x86\x90 Back", {backW, btnH}, false, st.step > 1, f))
            {
                st.step--;
            }

            ImGui::SameLine(0.0F, gap);

            if (!isReview)
            {
                if (DrawWizardButton("Next \xE2\x86\x92", {nextW, btnH}, true, true, f))
                {
                    st.step++;
                }
            }
            else
            {
                if (DrawWizardButton("Create Project", {createW, btnH}, true, !pathOccupied, f))
                {
                    st.open = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        // .modal { width:min(900px, calc(100vw - 56px)); height:min(680px, calc(100vh - 56px)); border-radius:8px; overflow:hidden }
        void DrawNewProjectModalImpl(NewProjectState &st, const Fonts &f, const ::ImTextureID logo)
        {
            using namespace WizardLayout;

            if (!st.open)
                return;

            const ImGuiViewport *vp = ImGui::GetMainViewport();
            const float modalW = std::min(ModalW, std::max(320.0F, vp->WorkSize.x - ViewportPad));
            const float modalH = std::min(ModalH, std::max(320.0F, vp->WorkSize.y - ViewportPad));
            const ImVec2 modalSize{modalW, modalH};
            const ImVec2 modalPos{
                vp->WorkPos.x + (vp->WorkSize.x - modalW) * 0.5F,
                vp->WorkPos.y + (vp->WorkSize.y - modalH) * 0.5F};

            DrawNewProjectBackdrop(vp, modalPos, modalSize);

            ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ModalRadius);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, WizardCss::Bg1());
            ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border());

            const bool isOpen = ImGui::BeginPopupModal(
                "New Project",
                &st.open,
                ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse);

            if (!isOpen)
            {
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(4);
                return;
            }

            const bool pathOccupied = PathLooksOccupied(st.path);

            if (DrawWizardHeader(st, f, logo))
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(4);
                return;
            }

            const float bodyH = ImGui::GetWindowHeight() - HeaderH - FooterH;

            // .body { flex:1; display:grid; grid-template-columns:220px 1fr; overflow:hidden }
            DrawWizardSidebar(st, f, bodyH);
            ImGui::SameLine(0.0F, 0.0F);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{MainPadX, MainPadY});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg1());
            ImGui::BeginChild("WizMain",
                              {0.0F, bodyH},
                              false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding);

            switch (st.step)
            {
            case 1:
                DrawStepTemplate(st, f);
                break;
            case 2:
                DrawStepIdentity(st, f, pathOccupied);
                break;
            case 3:
                DrawStepSettings(st, f);
                break;
            case 4:
                DrawStepReview(st, f, pathOccupied);
                break;
            default:
                st.step = 1;
                DrawStepTemplate(st, f);
                break;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            DrawWizardFooter(st, f, pathOccupied);

            ImGui::EndPopup();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
        }

    } // namespace

    /** @copydoc DrawNewProjectModal */
    void DrawNewProjectModal(NewProjectState &state, const Theme::Fonts &fonts, const ::ImTextureID logo)
    {
        DrawNewProjectModalImpl(state, fonts, logo);
    }

} // namespace Horo::Editor
