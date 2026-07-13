#include "Horo/Editor/EditorConfiguration.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace Horo::Editor
{
namespace
{
constexpr const char *kThemeKey = "editor.theme.active";
constexpr const char *kAccentColorKey = "editor.appearance.accent_color";
constexpr const char *kUiScaleKey = "editor.appearance.ui_scale_percent";
constexpr const char *kCodeFontSizeKey = "editor.appearance.code_font_size_px";

void RegisterAppearanceDescriptor(ConfigurationSchema &schema, const char *key, const SettingValueType type,
                                  SettingValue defaultValue)
{
    const SettingDescriptor descriptor{
        .key = SettingKey{key},
        .type = type,
        .defaultValue = std::move(defaultValue),
        .scope = SettingScope::User,
        .reloadPolicy = ReloadPolicy::NextFrame,
        .sensitivity = SettingSensitivity::Public,
    };
    assert(schema.Register(descriptor).HasValue());
}
} // namespace

/** @copydoc ToConfigurationThemeValue */
std::string_view ToConfigurationThemeValue(const EditorThemePreset preset) noexcept
{
    using enum EditorThemePreset;
    switch (preset)
    {
    case Midnight:
        return "midnight";
    case Light:
        return "light";
    case HoroDark:
    default:
        return "horo_dark";
    }
}

/** @copydoc ThemePresetFromConfigurationValue */
EditorThemePreset ThemePresetFromConfigurationValue(const std::string_view value) noexcept
{
    if (value == "midnight")
    {
        return EditorThemePreset::Midnight;
    }
    if (value == "light")
    {
        return EditorThemePreset::Light;
    }
    return EditorThemePreset::HoroDark;
}

/** @copydoc CreateEditorConfigurationService */
ConfigurationService CreateEditorConfigurationService(const EditorSettings &settings, EngineDataBus *events)
{
    ConfigurationSchema schema;
    RegisterAppearanceDescriptor(schema, kThemeKey, SettingValueType::String,
                                 std::string{ToConfigurationThemeValue(settings.themePreset)});
    RegisterAppearanceDescriptor(schema, kAccentColorKey, SettingValueType::String, settings.accentColorHex);
    RegisterAppearanceDescriptor(schema, kUiScaleKey, SettingValueType::Integer,
                                 settings.uiScalePercent);
    RegisterAppearanceDescriptor(schema, kCodeFontSizeKey, SettingValueType::Integer,
                                 settings.codeFontSizePx);
    assert(schema.Seal().HasValue());
    return ConfigurationService{std::move(schema), events};
}

/** @copydoc MakeEditorAppearanceConfigurationDraft */
ConfigurationDraft MakeEditorAppearanceConfigurationDraft(const ConfigurationSnapshot &base,
                                                          const EditorSettings &settings)
{
    ConfigurationDraft draft{.baseRevision = base.Revision()};
    draft.proposedValues.try_emplace(SettingKey{kThemeKey},
                                     std::string{ToConfigurationThemeValue(settings.themePreset)});
    draft.proposedValues.try_emplace(SettingKey{kAccentColorKey}, settings.accentColorHex);
    draft.proposedValues.try_emplace(SettingKey{kUiScaleKey}, static_cast<std::int64_t>(settings.uiScalePercent));
    draft.proposedValues.try_emplace(SettingKey{kCodeFontSizeKey}, static_cast<std::int64_t>(settings.codeFontSizePx));
    return draft;
}
} // namespace Horo::Editor
