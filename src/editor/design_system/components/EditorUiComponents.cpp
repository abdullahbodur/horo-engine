/** @copydoc EditorUiComponents.h */

#include "Horo/Editor/EditorUiComponents.h"

#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Horo::Editor::Ui
{

    namespace
    {

        void PushControlStyle()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 5.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        }

        void PopControlStyle()
        {
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
        }

    } // namespace

    // ── Button ───────────────────────────────────────────────────────────

    [[nodiscard]] bool Button(const ButtonProps &props)
    {
        using namespace Theme;

        if (!props.enabled)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45F);
        }

        if (props.variant == ButtonVariant::Primary)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Accent());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHover());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentActive());
            ImGui::PushStyleColor(ImGuiCol_Text, DarkText());
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Bg3());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentSoft());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{Accent().x, Accent().y, Accent().z, 0.24F});
            ImGui::PushStyleColor(ImGuiCol_Text, Text());
        }

        bool clicked = false;
        {
            ScopedTextStyle textStyle(props.font, props.fontSize, props.baseFontSize);
            clicked = ImGui::Button(props.label, props.size);
        }

        ImGui::PopStyleColor(4);
        if (!props.enabled)
        {
            ImGui::PopStyleVar();
        }
        return clicked;
    }

    // ── ScopedCard ───────────────────────────────────────────────────────

    ScopedCard::ScopedCard(const char *id, const ImVec2 size, const float padX, const float padY, const ImVec4 bg, const bool autoResizeY)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padX, padY});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGuiChildFlags childFlags = ImGuiChildFlags_Borders;
        if (autoResizeY)
        {
            childFlags |= (ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize);
        }
        ImGui::BeginChild(id, size, childFlags, ImGuiWindowFlags_NoScrollbar);
    }

    ScopedCard::~ScopedCard()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ── IconCloseButton ──────────────────────────────────────────────────

    [[nodiscard]] bool IconCloseButton(const char *id, const ImVec2 size)
    {
        using namespace Theme;

        ImGui::PushID(id);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##close", size);
        const bool hovered = ImGui::IsItemHovered();

        auto *dl = ImGui::GetWindowDrawList();
        const float pad = 4.0F;
        const ImVec2 a{pos.x + pad, pos.y + pad};
        const ImVec2 b{pos.x + size.x - pad, pos.y + size.y - pad};
        const ImU32 col = U32(hovered ? Text() : Dim());
        dl->AddLine(a, b, col, 1.5F);
        dl->AddLine({b.x, a.y}, {a.x, b.y}, col, 1.5F);

        ImGui::PopID();
        return clicked;
    }

    // ── SectionTitle ─────────────────────────────────────────────────────

    void SectionTitle(const char *upperCaseLabel, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle ts(fonts.monoSemiBold, 18.0F, Theme::FontPx::MonoSemiBold);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        ImGui::TextUnformatted(upperCaseLabel);
        ImGui::PopStyleColor();
    }

    // ── FieldLabel ───────────────────────────────────────────────────────

    void FieldLabel(const char *upperCaseLabel, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle ts(fonts.monoSemiBold, 12.0F, Theme::FontPx::MonoSemiBold);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextUnformatted(upperCaseLabel);
        ImGui::PopStyleColor();
    }

    // ── Hint ─────────────────────────────────────────────────────────────

    void Hint(const char *text, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle ts(fonts.sans, 12.0F, Theme::FontPx::Sans);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // ── ErrorText ────────────────────────────────────────────────────────

    void ErrorText(const char *text, const Theme::Fonts &fonts)
    {
        Theme::ScopedTextStyle ts(fonts.mono, 12.0F, Theme::FontPx::Mono);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // ── DashedSeparator ──────────────────────────────────────────────────

    void DashedSeparator(const float dash, const float gap)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        auto *dl = ImGui::GetWindowDrawList();
        float x = p.x;
        while (x < p.x + w)
        {
            const float end = std::min(x + dash, p.x + w);
            dl->AddLine({x, p.y}, {end, p.y}, Theme::U32(Theme::Border()), 1.0F);
            x = end + gap;
        }
        ImGui::Dummy({0.0F, 4.0F});
    }

    // ── SettingGroup ─────────────────────────────────────────────────────

    void SettingGroup(const char *label, const Theme::Fonts &fonts, const bool first)
    {
        if (!first)
        {
            ImGui::Dummy({0.0F, 18.0F});
        }

        Theme::ScopedTextStyle ts(fonts.monoSemiBold, 13.0F, Theme::FontPx::MonoSemiBold);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();

        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(p, {p.x + w, p.y}, Theme::U32(Theme::Border()), 1.0F);
        ImGui::Dummy({0.0F, 8.0F});
    }

    // ── ComboControl ─────────────────────────────────────────────────────

    bool ComboControl(const char *id, int *value, const char *const items[], const int itemCount, const Theme::Fonts &fonts, bool error)
    {
        bool changed = false;
        ImGui::PushID(id);

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
                          Theme::U32(fieldHovered ? Theme::Hover() : Theme::Bg3()), Theme::Layout::Radius);
        dl->AddRect(fieldPos, {fieldPos.x + fieldW, fieldPos.y + fieldH},
                    Theme::U32(error ? Theme::Err() : (popupOpen ? Theme::Accent() : Theme::Border())),
                    Theme::Layout::Radius, 0, popupOpen ? 1.5F : 1.0F);

        // Selected value label
        {
            ImFont *font = fonts.mono ? fonts.mono : ImGui::GetFont();
            const char *label = (*value >= 0 && *value < itemCount) ? items[*value] : "";
            dl->AddText(font, 15.0F,
                        {fieldPos.x + 10.0F, fieldPos.y + (fieldH - 15.0F) * 0.5F},
                        Theme::U32(Theme::Text()), label);
        }

        // Right-side arrow
        {
            const float cx = fieldPos.x + fieldW - 18.0F;
            const float cy = fieldPos.y + fieldH * 0.5F;
            const ImU32 arrowCol = Theme::U32(fieldHovered ? Theme::Text() : Theme::Muted());
            dl->AddTriangleFilled({cx - 4.0F, cy - 2.0F}, {cx + 4.0F, cy - 2.0F}, {cx, cy + 3.0F}, arrowCol);
        }

        if (fieldClicked)
            ImGui::OpenPopup(popupId.c_str());

        ImGui::SetNextWindowPos({fieldPos.x, fieldPos.y + fieldH + 4.0F});
        ImGui::SetNextWindowSize({fieldW, 0.0F});

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 5.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Bg2());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::BorderStrong());

        if (ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoMove))
        {
            const ImVec2 pMin = ImGui::GetWindowPos();
            const ImVec2 pMax = {pMin.x + ImGui::GetWindowWidth(), pMin.y + ImGui::GetWindowHeight()};

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

                auto *wdl = ImGui::GetWindowDrawList();
                if (rowHovered || isSelected)
                {
                    wdl->AddRectFilled(rowMin, {rowMin.x + rowW, rowMin.y + rowH},
                                       Theme::U32(isSelected ? Theme::Hover() : Theme::Hover()));
                }
                wdl->AddText(fonts.mono ? fonts.mono : ImGui::GetFont(), 14.0F,
                             {rowMin.x + 14.0F, rowMin.y + (rowH - 14.0F) * 0.5F},
                             Theme::U32(isSelected ? Theme::Text() : Theme::Muted()), items[i]);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::PopID();
        return changed;
    }

    // ── InputTextControl ─────────────────────────────────────────────────

    bool InputTextControl(const char *id, char *buffer, const size_t bufferSize, const Theme::Fonts &fonts, bool error)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, error ? Theme::ErrSoft() : Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, error ? Theme::ErrSoft() : Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, error ? Theme::ErrSoft() : Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_Border, error ? Theme::Err() : Theme::Border());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());

        bool changed = false;
        ImGui::PushItemWidth(-1.0F);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            changed = ImGui::InputText(id, buffer, bufferSize);
        }
        ImGui::PopItemWidth();

        if (ImGui::IsItemActive())
        {
            const ImVec2 pMin = ImGui::GetItemRectMin();
            const ImVec2 pMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(pMin, pMax, Theme::U32(error ? Theme::ErrSoft() : Theme::AccentSoft()), Theme::Layout::Radius + 2.0F, 0, 2.0F);
        }
        else if (ImGui::IsItemHovered())
        {
            const ImVec2 pMin = ImGui::GetItemRectMin();
            const ImVec2 pMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(pMin, pMax, Theme::U32(Theme::BorderStrong()), Theme::Layout::Radius, 0, 1.0F);
        }

        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);
        return changed;
    }

    // ── ColorHexControl ───────────────────────────────────────────────────

    bool ColorHexControl(const char *id, char *buffer, const size_t bufferSize, const Theme::Fonts &fonts)
    {
        const auto hexDigit = [](const char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        const auto parse = [&hexDigit](const char *text, ImVec4 &out) -> bool {
            if (!text || text[0] != '#' || text[7] != '\0') return false;
            const int digits[6] = {hexDigit(text[1]), hexDigit(text[2]), hexDigit(text[3]),
                                   hexDigit(text[4]), hexDigit(text[5]), hexDigit(text[6])};
            for (const int digit : digits) if (digit < 0) return false;
            out = ImVec4{static_cast<float>(digits[0] * 16 + digits[1]) / 255.0F,
                         static_cast<float>(digits[2] * 16 + digits[3]) / 255.0F,
                         static_cast<float>(digits[4] * 16 + digits[5]) / 255.0F, 1.0F};
            return true;
        };
        const auto pack = [](const ImVec4 color) -> ImU32 { return ImGui::ColorConvertFloat4ToU32(color); };
        const auto unpack = [](const ImU32 color) -> ImVec4 { return ImGui::ColorConvertU32ToFloat4(color); };
        const auto writeCanonical = [buffer, bufferSize](const ImVec4 color) {
            const int red = static_cast<int>(color.x * 255.0F + 0.5F);
            const int green = static_cast<int>(color.y * 255.0F + 0.5F);
            const int blue = static_cast<int>(color.z * 255.0F + 0.5F);
            std::snprintf(buffer, bufferSize, "#%02X%02X%02X", red, green, blue);
        };

        ImGui::PushID(id);
        ImGuiStorage *const storage = ImGui::GetStateStorage();
        const ImGuiID lastValidKey = ImGui::GetID("last-valid-color");
        ImVec4 current{};
        if (parse(buffer, current))
        {
            storage->SetInt(lastValidKey, static_cast<int>(pack(current)));
        }
        else if (storage->GetInt(lastValidKey, 0) != 0)
        {
            current = unpack(static_cast<ImU32>(storage->GetInt(lastValidKey)));
        }
        else
        {
            current = Theme::Accent();
            storage->SetInt(lastValidKey, static_cast<int>(pack(current)));
        }

        const ImVec2 swatchPosition = ImGui::GetCursorScreenPos();
        constexpr ImVec2 swatchSize{34.0F, 34.0F};
        ImGui::InvisibleButton("swatch", swatchSize);
        const bool openPicker = ImGui::IsItemClicked();
        ImDrawList *const drawList = ImGui::GetWindowDrawList();
        const ImVec2 swatchEnd{swatchPosition.x + swatchSize.x, swatchPosition.y + swatchSize.y};
        drawList->AddRectFilled(swatchPosition, swatchEnd, ImGui::ColorConvertFloat4ToU32(current), Theme::Layout::Radius);
        drawList->AddRect(swatchPosition, swatchEnd, Theme::U32(Theme::Border()), Theme::Layout::Radius);
        if (openPicker) ImGui::OpenPopup("picker");

        ImGui::SameLine(0.0F, 8.0F);
        PushControlStyle();
        ImGui::PushItemWidth(-1.0F);
        bool validChange = false;
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            if (ImGui::InputText("hex", buffer, bufferSize) && parse(buffer, current))
            {
                storage->SetInt(lastValidKey, static_cast<int>(pack(current)));
                validChange = true;
            }
        }
        ImGui::PopItemWidth();
        PopControlStyle();

        if (ImGui::BeginPopup("picker"))
        {
            ImGui::TextUnformatted("Accent color");
            ImGui::Separator();
            if (ImGui::ColorPicker3("##color-picker", &current.x, ImGuiColorEditFlags_NoSidePreview))
            {
                writeCanonical(current);
                storage->SetInt(lastValidKey, static_cast<int>(pack(current)));
                validChange = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return validChange;
    }

    // ── InputIntControl ──────────────────────────────────────────────────

    void InputIntControl(const char *id, int *value, const Theme::Fonts &fonts)
    {
        PushControlStyle();
        ImGui::PushItemWidth(-1.0F);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            ImGui::InputInt(id, value, 1, 4);
        }
        ImGui::PopItemWidth();
        PopControlStyle();
    }

    // ── InputFloatControl ────────────────────────────────────────────────

    void InputFloatControl(const char *id, float *value, const Theme::Fonts &fonts)
    {
        PushControlStyle();
        ImGui::PushItemWidth(-1.0F);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            ImGui::InputFloat(id, value, 0.1F, 1.0F, "%.1f");
        }
        ImGui::PopItemWidth();
        PopControlStyle();
    }

    // ── SliderIntControl ─────────────────────────────────────────────────

    void SliderIntControl(const char *id,
                          int *value,
                          const int minValue,
                          const int maxValue,
                          const char *suffix,
                          const Theme::Fonts &fonts,
                          const int step)
    {
        ImGui::PushID(id);
        constexpr float TrackW = Theme::Layout::ControlW - 54.0F;
        constexpr float TrackH = 4.0F;
        constexpr float HitH = 22.0F;
        constexpr float KnobR = 7.0F;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("slider", {TrackW, HitH});
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();

        if (active && maxValue > minValue)
        {
            const float mouseT = (ImGui::GetIO().MousePos.x - pos.x) / TrackW;
            const float clampedT = std::clamp(mouseT, 0.0F, 1.0F);
            const float rawValue = static_cast<float>(minValue) +
                                   clampedT * static_cast<float>(maxValue - minValue);
            const int snapped = minValue +
                                static_cast<int>(std::round((rawValue - static_cast<float>(minValue)) /
                                                            static_cast<float>(step))) *
                                    step;
            *value = std::clamp(snapped, minValue, maxValue);
        }

        const float t = maxValue > minValue
                            ? (static_cast<float>(*value - minValue) / static_cast<float>(maxValue - minValue))
                            : 0.0F;
        const float trackY = pos.y + (HitH - TrackH) * 0.5F;
        auto *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({pos.x, trackY}, {pos.x + TrackW, trackY + TrackH}, Theme::U32(Theme::BorderStrong()), 2.0F);
        dl->AddRectFilled({pos.x, trackY}, {pos.x + TrackW * t, trackY + TrackH}, Theme::U32(Theme::Accent()), 2.0F);

        const ImVec2 knob{pos.x + TrackW * t, pos.y + HitH * 0.5F};
        dl->AddCircleFilled(knob, KnobR + (hovered || active ? 1.0F : 0.0F), Theme::U32(Theme::AccentHover()), 20);
        dl->AddCircleFilled(knob, KnobR, Theme::U32(Theme::Accent()), 20);
        dl->AddCircle(knob, KnobR + 1.0F, Theme::U32(Theme::Bg1()), 20, 2.0F);

        ImGui::SameLine(0.0F, 10.0F);

        char text[32]{};
        // cppcheck-suppress wrongPrintfScanfArgNum — suffix is a format string
        std::snprintf(text, sizeof(text), suffix, *value);
        {
            Theme::ScopedTextStyle ts(fonts.mono, 14.0F, Theme::FontPx::Mono);
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (HitH - textSize.y) * 0.5F);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }

    // ── ToggleControl ────────────────────────────────────────────────────

    [[nodiscard]] bool ToggleControl(const char *id, bool *value, const Theme::Fonts &fonts, const bool showLabel)
    {
        ImGui::PushID(id);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size{36.0F, 20.0F};
        ImGui::InvisibleButton("toggle", size);
        const bool clicked = ImGui::IsItemClicked();
        if (clicked)
        {
            *value = !*value;
        }

        const bool hovered = ImGui::IsItemHovered();
        auto *dl = ImGui::GetWindowDrawList();
        ImVec4 bg = Theme::Bg3();
        if (*value)
            bg = Theme::Accent();
        else if (hovered)
            bg = Theme::Hover();
        dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, Theme::U32(bg), 10.0F);
        dl->AddRect(pos, {pos.x + size.x, pos.y + size.y}, Theme::U32(*value ? Theme::Accent() : Theme::Border()), 10.0F);
        const float knobX = *value ? pos.x + 21.0F : pos.x + 3.0F;
        dl->AddCircleFilled({knobX + 6.0F, pos.y + 10.0F}, 6.0F, Theme::U32(*value ? ImVec4{1, 1, 1, 1} : Theme::Dim()), 16);

        if (showLabel)
        {
            ImGui::SameLine(0.0F, 10.0F);
            Theme::ScopedTextStyle ts(fonts.sans, 13.0F, Theme::FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
            ImGui::TextUnformatted(*value ? "Enabled" : "Disabled");
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
        return clicked;
    }

    // ── CheckboxControl ──────────────────────────────────────────────────

    [[nodiscard]] bool CheckboxControl(const char *label, bool *value, const Theme::Fonts &fonts)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0F, 0.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{8.0F, 0.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleColor(ImGuiCol_CheckMark, Theme::Accent());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());

        bool clicked = false;
        {
            Theme::ScopedTextStyle ts(fonts.sans, 13.0F, Theme::FontPx::Sans);
            clicked = ImGui::Checkbox(label, value);
        }

        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(4);
        return clicked;
    }

    // ── PluginRow ────────────────────────────────────────────────────────

    void PluginRow(const char *name,
                   const char *version,
                   const char *description,
                   bool *enabled,
                   const Theme::Fonts &fonts)
    {
        SettingRow(name, nullptr, fonts, [&]() {
            const float cursorY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(cursorY + 4.0F);
            {
                Theme::ScopedTextStyle ts(fonts.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::TextUnformatted(version);
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0F);
            {
                Theme::ScopedTextStyle ts(fonts.sans, 12.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Theme::Layout::ControlW - 52.0F);
                ImGui::TextWrapped("%s", description);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorPos({Theme::Layout::ControlW - 42.0F, cursorY + 6.0F});
            (void)ToggleControl("plugin-toggle", enabled, fonts, false);
        });
    }

    // ── ShortcutDisplay ──────────────────────────────────────────────────

    void ShortcutDisplay(const char *a, const char *b, const char *c, const Theme::Fonts &fonts)
    {
        const std::array<const char *, 3> keys = {a, b, c};
        for (int i = 0; i < 3; ++i)
        {
            if (keys[i] == nullptr || keys[i][0] == '\0')
                continue;
            if (i > 0)
            {
                ImGui::SameLine(0.0F, 4.0F);
                {
                    Theme::ScopedTextStyle plus(fonts.mono, 12.0F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted("+");
                    ImGui::PopStyleColor();
                }
                ImGui::SameLine(0.0F, 4.0F);
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{7.0F, 3.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            {
                Theme::ScopedTextStyle keyText(fonts.mono, 12.0F, Theme::FontPx::Mono);
                ImGui::Button(keys[i], ImVec2{0.0F, 24.0F});
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
        }
    }

    // ── ShortcutRecorder ──────────────────────────────────────────────────

    [[nodiscard]] bool ShortcutRecorder(const char *id,
                                        const char *keysLabel,
                                        bool *listening,
                                        char *keysOut,
                                        const int keysOutSize,
                                        const Theme::Fonts &fonts)
    {
        using namespace Theme;
        ImGui::PushID(id);

        bool recorded = false;

        if (*listening)
        {
            // ── Poll key state ──────────────────────────────────────────
            const auto &io = ImGui::GetIO();

            // Escape cancels listening
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                *listening = false;
                ImGui::PopID();
                return false;
            }

            const bool ctrl = io.KeyCtrl || io.KeySuper;
            const bool shift = io.KeyShift;
            const bool alt = io.KeyAlt;

            // Check non-modifier keys (A-Z, 0-9)
            for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
            {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false))
                {
                    std::string combo;
                    if (ctrl) combo += "Ctrl+";
                    if (shift) combo += "Shift+";
                    if (alt) combo += "Alt+";
                    combo += ImGui::GetKeyName(static_cast<ImGuiKey>(k));

                    std::snprintf(keysOut, static_cast<std::size_t>(keysOutSize),
                                  "%s", combo.c_str());
                    *listening = false;
                    recorded = true;
                    ImGui::PopID();
                    return true;
                }
            }

            // Check F-keys
            for (int k = ImGuiKey_F1; k <= ImGuiKey_F12; ++k)
            {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false))
                {
                    std::string combo;
                    if (ctrl) combo += "Ctrl+";
                    if (shift) combo += "Shift+";
                    if (alt) combo += "Alt+";
                    combo += ImGui::GetKeyName(static_cast<ImGuiKey>(k));

                    std::snprintf(keysOut, static_cast<std::size_t>(keysOutSize),
                                  "%s", combo.c_str());
                    *listening = false;
                    recorded = true;
                    ImGui::PopID();
                    return true;
                }
            }

            // Special keys
            static constexpr ImGuiKey kSpecial[] = {
                ImGuiKey_Space, ImGuiKey_Tab, ImGuiKey_Backspace,
                ImGuiKey_Delete, ImGuiKey_Enter, ImGuiKey_Home, ImGuiKey_End,
                ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
                ImGuiKey_UpArrow, ImGuiKey_DownArrow,
                ImGuiKey_PageUp, ImGuiKey_PageDown,
            };
            for (const auto k : kSpecial)
            {
                if (ImGui::IsKeyPressed(k, false))
                {
                    std::string combo;
                    if (ctrl) combo += "Ctrl+";
                    if (shift) combo += "Shift+";
                    if (alt) combo += "Alt+";
                    combo += ImGui::GetKeyName(k);

                    std::snprintf(keysOut, static_cast<std::size_t>(keysOutSize),
                                  "%s", combo.c_str());
                    *listening = false;
                    recorded = true;
                    ImGui::PopID();
                    return true;
                }
            }
        }

        // ── Draw the recorder UI ────────────────────────────────────────
        const float width = Layout::ControlW;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 size = {width, 28.0F};

        // Dashed border background
        auto *dl = ImGui::GetWindowDrawList();
        const ImU32 borderCol = *listening ? U32(Accent()) : U32(BorderStrong());
        const ImU32 bgCol = *listening
                                ? ImGui::GetColorU32(ImVec4{Accent().x, Accent().y, Accent().z, 0.06F})
                                : U32(ImVec4{0.0F, 0.0F, 0.0F, 0.0F});

        dl->AddRectFilled(cursor, {cursor.x + size.x, cursor.y + size.y},
                          bgCol, Layout::Radius);

        // Draw dashed border manually (4px dash segments)
        const ImU32 dashCol = borderCol;
        const float r = Layout::Radius;
        const auto drawDashRect = [&](ImVec2 p0, ImVec2 p1) {
            // Simple solid border for now — dashed is complex in ImDrawList
            dl->AddRect(p0, p1, dashCol, r, 0, 1.0F);
        };
        drawDashRect(cursor, {cursor.x + size.x, cursor.y + size.y});

        // Invisible button for click detection
        ImGui::SetCursorScreenPos(cursor);
        ImGui::InvisibleButton("recorder", size);

        if (ImGui::IsItemClicked() && !(*listening))
        {
            *listening = true;
        }

        // Draw content on top
        if (*listening)
        {
            const char *text = "Press keys...";
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            dl->AddText(fonts.mono, 11.0F,
                        {cursor.x + (size.x - textSize.x) * 0.5F,
                         cursor.y + (size.y - textSize.y) * 0.5F},
                        U32(Accent()), text);
        }
        else if (keysLabel && keysLabel[0] != '\0')
        {
            // Parse keysLabel "Ctrl+Shift+B" into individual kbd chips
            std::string label{keysLabel};
            std::vector<std::string> parts;
            std::size_t pos = 0;
            while (pos < label.size())
            {
                auto next = label.find('+', pos);
                parts.push_back(label.substr(pos, next - pos));
                if (next == std::string::npos) break;
                pos = next + 1;
            }

            float totalW = 0.0F;
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                const auto &p = parts[i];
                const ImVec2 ts = ImGui::CalcTextSize(p.c_str());
                totalW += ts.x + 10.0F; // padding
                if (i < parts.size() - 1) totalW += 8.0F; // "+"
            }

            float x = cursor.x + (size.x - totalW) * 0.5F;
            const float y = cursor.y + (size.y - 14.0F) * 0.5F;
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                const auto &p = parts[i];
                const ImVec2 ts = ImGui::CalcTextSize(p.c_str());
                const ImVec2 chipMin = {x, y};
                const ImVec2 chipMax = {x + ts.x + 10.0F, y + 18.0F};

                dl->AddRectFilled(chipMin, chipMax, U32(Bg3()), 3.0F);
                dl->AddRect(chipMin, chipMax, U32(BorderStrong()), 3.0F, 0, 1.0F);
                dl->AddText(fonts.mono, 10.5F,
                            {x + 5.0F, y + 2.0F}, U32(Text()), p.c_str());

                x += ts.x + 10.0F;
                if (i < parts.size() - 1)
                {
                    dl->AddText(fonts.mono, 10.0F,
                                {x + 2.0F, y + 2.0F}, U32(Dim()), "+");
                    x += 12.0F;
                }
            }
        }
        else
        {
            const char *placeholder = "Click to record";
            const ImVec2 ts = ImGui::CalcTextSize(placeholder);
            dl->AddText(fonts.mono, 11.0F,
                        {cursor.x + (size.x - ts.x) * 0.5F,
                         cursor.y + (size.y - ts.y) * 0.5F},
                        U32(Dim()), placeholder);
        }

        ImGui::PopID();
        return recorded;
    }

    // ── ThemeChip ────────────────────────────────────────────────────────

    [[nodiscard]] bool ThemeChip(const char *label, const ImVec4 swatch, const bool active, const Theme::Fonts &fonts)
    {
        ImGui::PushID(label);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{12.0F, 7.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);

        const auto accentGlow = ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.14F};
        ImGui::PushStyleColor(ImGuiCol_Button, active ? accentGlow : Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Hover());
        ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
        ImGui::PushStyleColor(ImGuiCol_Border, active ? Theme::Accent() : Theme::Border());

        bool clicked = false;
        {
            Theme::ScopedTextStyle ts(fonts.mono, 12.0F, Theme::FontPx::Mono);
            clicked = ImGui::Button(label, ImVec2{82.0F, 32.0F});
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddCircleFilled({min.x + 12.0F, min.y + 16.0F}, 5.0F, Theme::U32(swatch), 16);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);
        ImGui::PopID();
        return clicked;
    }

} // namespace Horo::Editor::Ui
