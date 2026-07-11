#pragma once

#include <cstdint>
#include <string_view>

namespace Horo::Editor
{
    enum class SettingsChangePhase { Preview, Committed, Reverted };

    enum class SettingsDomain : std::uint32_t
    {
        None = 0,
        Appearance = 1u << 0u,
        Localization = 1u << 1u,
        All = 0xffffffffu,
    };

    constexpr SettingsDomain operator|(const SettingsDomain lhs, const SettingsDomain rhs) noexcept
    {
        return static_cast<SettingsDomain>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    /** @brief Small invalidation notification; subscribers query the settings authority. */
    struct EditorSettingsChangedEvent
    {
        static constexpr std::string_view HoroEventTypeName = "horo.editor.EditorSettingsChangedEvent";
        std::uint64_t revision = 0;
        SettingsChangePhase phase = SettingsChangePhase::Preview;
        SettingsDomain changedDomains = SettingsDomain::None;
    };
} // namespace Horo::Editor
