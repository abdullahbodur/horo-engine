#pragma once

#include "Horo/Editor/DesignSystem/DesignTokens.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Horo::Editor::Theme {

// ─────────────────────────────────────────────────────────────────────────
// Palette — exact match for the CSS custom properties in the HTML mockups:
//   --bg0 #0a0c0f   --bg1 #12151a   --bg2 #181c21   --bg3 #1f242b
//   --hover #232830 --bd #2a2f37    --bd2 #3a4049
//   --txt #e8e4d9   --mut #9a958a   --dim #5e5b54
//   --a #04A5FC     --ok #5fb88a    --warn #e8a33d  --err #d4524a
//
// These read from the active DesignTokens so theme switching
// affects all custom-drawn editor UI, not just ImGui native widgets.
// ─────────────────────────────────────────────────────────────────────────

/** @brief Returns the active (theme-aware) design token set. */
[[nodiscard]] const DesignSystem::DesignTokens &GetActiveTokens();

[[nodiscard]] inline ::ImVec4 Bg0()          { return GetActiveTokens().colors.surfaceRoot; }
[[nodiscard]] inline ::ImVec4 Bg1()          { return GetActiveTokens().colors.surfaceWindow; }
[[nodiscard]] inline ::ImVec4 Bg2()          { return GetActiveTokens().colors.surfacePanel; }
[[nodiscard]] inline ::ImVec4 Bg3()          { return GetActiveTokens().colors.surfaceRaised; }
[[nodiscard]] inline ::ImVec4 Hover()        { return GetActiveTokens().colors.surfaceHover; }
[[nodiscard]] inline ::ImVec4 Border()       { return GetActiveTokens().colors.border; }
[[nodiscard]] inline ::ImVec4 BorderStrong() { return GetActiveTokens().colors.borderStrong; }
[[nodiscard]] inline ::ImVec4 Text()         { return GetActiveTokens().colors.textPrimary; }
[[nodiscard]] inline ::ImVec4 Muted()        { return GetActiveTokens().colors.textMuted; }
[[nodiscard]] inline ::ImVec4 Dim()          { return GetActiveTokens().colors.textDim; }
[[nodiscard]] inline ::ImVec4 Accent()       { return GetActiveTokens().colors.actionPrimary; }
[[nodiscard]] inline ::ImVec4 AccentHover()  { return GetActiveTokens().colors.actionPrimaryHover; }
[[nodiscard]] inline ::ImVec4 AccentActive() { return GetActiveTokens().colors.actionPrimaryActive; }
[[nodiscard]] inline ::ImVec4 AccentSoft()   { return GetActiveTokens().colors.actionPrimarySoft; }
[[nodiscard]] inline ::ImVec4 Ok()           { return GetActiveTokens().colors.statusOk; }
[[nodiscard]] inline ::ImVec4 Warn()         { return GetActiveTokens().colors.statusWarn; }
[[nodiscard]] inline ::ImVec4 Err()          { return GetActiveTokens().colors.statusError; }
[[nodiscard]] inline ::ImVec4 ErrSoft()      { ::ImVec4 c = GetActiveTokens().colors.statusError; c.w = 0.12F; return c; }
[[nodiscard]] inline ::ImVec4 DarkText()     { return GetActiveTokens().colors.textOnActionPrimary; }
[[nodiscard]] inline ::ImVec4 Shadow()       { return {0.0F, 0.0F, 0.0F, 0.55F}; }

[[nodiscard]] inline ::ImU32 U32(const ::ImVec4& c) { return ImGui::GetColorU32(c); }

// ─────────────────────────────────────────────────────────────────────────
// Fonts — the three font atlas entries loaded by the application
// ─────────────────────────────────────────────────────────────────────────
struct Fonts {
    ::ImFont* sans         = nullptr;
    ::ImFont* mono         = nullptr;
    ::ImFont* monoSemiBold = nullptr;
};

namespace FontPx {
    constexpr float Sans         = DesignSystem::DefaultDesignTokens().typography.sansBase;
    constexpr float Mono         = DesignSystem::DefaultDesignTokens().typography.monoBase;
    constexpr float MonoSemiBold = DesignSystem::DefaultDesignTokens().typography.monoSemiBoldBase;
}

[[nodiscard]] constexpr float Scale(float targetPx, float basePx) { return targetPx / basePx; }

inline void PushFont(::ImFont* f) { if (f) ImGui::PushFont(f); }
inline void PopFont(::ImFont* f)  { if (f) ImGui::PopFont(); }

struct ScopedFont {
    ::ImFont* font;
    explicit ScopedFont(::ImFont* f) : font(f) { PushFont(font); }
    ~ScopedFont() { PopFont(font); }
    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
};

struct ScopedFontScale {
    explicit ScopedFontScale(float scale) { ImGui::SetWindowFontScale(scale); }
    ~ScopedFontScale() { ImGui::SetWindowFontScale(1.0F); }
    ScopedFontScale(const ScopedFontScale&) = delete;
    ScopedFontScale& operator=(const ScopedFontScale&) = delete;
};

struct ScopedTextStyle {
    [[no_unique_address]] ScopedFont font;
    [[no_unique_address]] ScopedFontScale scale;
    ScopedTextStyle(::ImFont* f, float targetPx, float basePx)
        : font(f), scale(Scale(targetPx, basePx)) {}
};

// ─────────────────────────────────────────────────────────────────────────
// Layout metrics
// ─────────────────────────────────────────────────────────────────────────
namespace Layout {
    constexpr float Radius      = DesignSystem::DefaultDesignTokens().radii.control;
    constexpr float RadiusCard  = DesignSystem::DefaultDesignTokens().radii.card;
    constexpr float RadiusModal = DesignSystem::DefaultDesignTokens().radii.modal;
    constexpr float WelcomeOuterPad = 40.0F;
    constexpr float WelcomeCardW    = 900.0F;
    constexpr float WelcomeSideW    = DesignSystem::DefaultDesignTokens().sizes.welcomeSideWidth;
    constexpr float WelcomePad      = DesignSystem::DefaultDesignTokens().sizes.welcomePadding;
    constexpr float ModalW      = DesignSystem::DefaultDesignTokens().sizes.modalWidth;
    constexpr float ModalH      = DesignSystem::DefaultDesignTokens().sizes.modalHeight;
    constexpr float HeaderH     = DesignSystem::DefaultDesignTokens().sizes.modalHeaderHeight;
    constexpr float FooterH     = DesignSystem::DefaultDesignTokens().sizes.modalFooterHeight;
    constexpr float SidebarW    = DesignSystem::DefaultDesignTokens().sizes.modalSidebarWidth;
    constexpr float SidebarPadX = DesignSystem::DefaultDesignTokens().spacing.sidebarPaddingX;
    constexpr float SidebarPadY = DesignSystem::DefaultDesignTokens().spacing.sidebarPaddingY;
    constexpr float BodyPadX    = DesignSystem::DefaultDesignTokens().spacing.bodyPaddingX;
    constexpr float BodyPadY    = DesignSystem::DefaultDesignTokens().spacing.bodyPaddingY;
    constexpr float CardPad     = DesignSystem::DefaultDesignTokens().spacing.cardPadding;
    constexpr float GridGap     = DesignSystem::DefaultDesignTokens().spacing.gridGap;
    constexpr float SettingsW   = DesignSystem::DefaultDesignTokens().sizes.settingsWidth;
    constexpr float SettingsH   = DesignSystem::DefaultDesignTokens().sizes.settingsHeight;
    constexpr float ControlW    = 260.0F;
}

// ─────────────────────────────────────────────────────────────────────────
// Theme preset & runtime switching
// ─────────────────────────────────────────────────────────────────────────
enum class Preset
{
    HoroDark = 0,
    Midnight = 1,
    Light    = 2,
    // Custom themes start at index 3+
};

struct ThemeStringHash
{
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
    [[nodiscard]] std::size_t operator()(const std::string &value) const noexcept { return (*this)(std::string_view{value}); }
    [[nodiscard]] std::size_t operator()(const char *value) const noexcept { return (*this)(std::string_view{value}); }
};

/**
 * @brief A loaded theme entry — either built-in or from a JSON file.
 */
struct ThemeEntry
{
    std::string name;           // display name (e.g. "Monokai")
    std::string sourcePath;     // empty for built-in, file path for custom
    std::unordered_map<std::string, ::ImVec4, ThemeStringHash, std::equal_to<>> colors; // key → RGBA
    bool isBuiltIn = true;
};

/** @brief Returns the list of all discovered themes (built-in + custom). */
[[nodiscard]] const std::vector<ThemeEntry> &GetThemeList();

/** @brief Scans `~/.horo/themes/` for JSON theme files. Call once at startup. */
void RefreshThemeList(const char *additionalPath = nullptr);

/** @brief Loads a single theme JSON from the given path. Returns true on success. */
[[nodiscard]] bool LoadThemeFromJson(const char *path, ThemeEntry &outEntry);

/** @brief Activates a theme by index into GetThemeList(). */
void SelectThemeByIndex(int index);

/** @brief Returns the currently active theme index. */
[[nodiscard]] int GetActiveThemeIndex();

void Apply(ImGuiStyle &style);
void SetThemePreset(Preset preset);
[[nodiscard]] Preset GetThemePreset();
void ApplyCurrentTheme();

} // namespace Horo::Editor::Theme
