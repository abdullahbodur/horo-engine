#include "Horo/Gameplay/Component.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <cctype>

namespace Horo::Gameplay {
    namespace {
        [[nodiscard]] bool IsLowercaseIdentifier(const std::string_view value, const std::size_t maximumBytes) noexcept {
            if (value.empty() || value.size() > maximumBytes)
                return false;
            return std::ranges::all_of(value, [](const unsigned char character) {
                return character == '_' || std::islower(character) || std::isdigit(character);
            });
        }

        [[nodiscard]] bool IsNamespacedComponentId(const std::string_view value) noexcept {
            if (value.size() < 7 || value.size() > MaximumComponentTypeIdBytes || !value.starts_with("game."))
                return false;
            bool previousDot = false;
            std::size_t dotCount = 0;
            for (const unsigned char character : value) {
                const bool dot = character == '.';
                if ((!dot && character != '_' && !std::islower(character) && !std::isdigit(character)) || (dot && previousDot))
                    return false;
                dotCount += dot ? 1U : 0U;
                previousDot = dot;
            }
            return dotCount >= 2 && !previousDot;
        }
    }  // namespace

    /** @copydoc ComponentTypeId::Parse */
    Result<ComponentTypeId> ComponentTypeId::Parse(const std::string_view value) {
        if (!IsNamespacedComponentId(value))
            return Result<ComponentTypeId>::Failure(MakeError(GameplayErrors::InvalidComponentTypeId));
        return Result<ComponentTypeId>::Success(ComponentTypeId{std::string{value}});
    }

    /** @copydoc ComponentTypeId::Value */
    const std::string &ComponentTypeId::Value() const noexcept {
        return value_;
    }

    /** @copydoc ComponentTypeId::IsValid */
    bool ComponentTypeId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc ComponentPropertyId::Parse */
    Result<ComponentPropertyId> ComponentPropertyId::Parse(const std::string_view value) {
        if (!IsLowercaseIdentifier(value, MaximumComponentPropertyIdBytes))
            return Result<ComponentPropertyId>::Failure(MakeError(GameplayErrors::InvalidComponentDescriptor));
        return Result<ComponentPropertyId>::Success(ComponentPropertyId{std::string{value}});
    }

    /** @copydoc ComponentPropertyId::Value */
    const std::string &ComponentPropertyId::Value() const noexcept {
        return value_;
    }

    /** @copydoc ComponentPropertyId::IsValid */
    bool ComponentPropertyId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc ValidateSerializedComponent */
    Result<void> ValidateSerializedComponent(const SerializedComponent &component) {
        if (!component.typeId.IsValid() || component.schemaVersion == 0 || component.payload.size() > MaximumSerializedComponentBytes)
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidSerializedComponent));
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay
