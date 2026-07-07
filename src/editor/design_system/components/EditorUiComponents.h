#pragma once

#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

namespace Horo::Editor::Ui
{

    /** @brief Semantic button variant for shared editor buttons. */
    enum class ButtonVariant
    {
        Primary,
        Secondary,
    };

    /** @brief Input contract for the shared editor button primitive. */
    struct ButtonProps
    {
        const char *label = "";
        ImVec2 size = {0.0F, 0.0F};
        ButtonVariant variant = ButtonVariant::Primary;
        bool enabled = true;
        float fontSize = 14.0F;
        ImFont *font = nullptr;
        float baseFontSize = Theme::FontPx::Sans;
    };

    /** @brief RAII child surface matching the shared card visual contract. */
    class ScopedCard
    {
    public:
        explicit ScopedCard(const char *id,
                            ImVec2 size,
                            float padX = Theme::Layout::CardPad,
                            float padY = Theme::Layout::CardPad,
                            ImVec4 bg = Theme::Bg2());
        ~ScopedCard();

        ScopedCard(const ScopedCard &) = delete;
        ScopedCard &operator=(const ScopedCard &) = delete;
    };

    /** @brief Draws a shared editor button primitive. */
    [[nodiscard]] bool Button(const ButtonProps &props);

    /** @brief Draws an icon-only close button using vector strokes, not glyph text. */
    [[nodiscard]] bool IconCloseButton(const char *id, ImVec2 size);

    /** @brief Draws an uppercase section label. */
    void SectionTitle(const char *upperCaseLabel, const Theme::Fonts &fonts);

    /** @brief Draws an uppercase field label. */
    void FieldLabel(const char *upperCaseLabel, const Theme::Fonts &fonts);

    /** @brief Draws muted wrapped helper text. */
    void Hint(const char *text, const Theme::Fonts &fonts);

    /** @brief Draws a dashed horizontal separator across available width. */
    void DashedSeparator(float dash = 4.0F, float gap = 3.0F);

} // namespace Horo::Editor::Ui
