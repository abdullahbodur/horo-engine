#include "Horo/Gameplay/BehaviorTypes.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace Horo::Gameplay {
    namespace {
        [[nodiscard]] bool ValidNamespacedId(const std::string_view value) noexcept {
            if (value.size() < 7 || value.size() > MaximumBehaviorTypeIdBytes || !value.starts_with("game."))
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

    /** @copydoc BehaviorTypeId::Parse */
    Result<BehaviorTypeId> BehaviorTypeId::Parse(const std::string_view value) {
        if (!ValidNamespacedId(value))
            return Result<BehaviorTypeId>::Failure(MakeError(GameplayErrors::InvalidBehaviorTypeId));
        return Result<BehaviorTypeId>::Success(BehaviorTypeId{std::string{value}});
    }

    /** @copydoc BehaviorTypeId::Value */
    const std::string &BehaviorTypeId::Value() const noexcept {
        return value_;
    }

    /** @copydoc BehaviorTypeId::IsValid */
    bool BehaviorTypeId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc ValidateBehaviorComponent */
    Result<void> ValidateBehaviorComponent(const BehaviorComponent &component) {
        if (!component.instanceId.IsValid())
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidBehaviorInstanceId));
        if (!component.typeId.IsValid() || component.schemaVersion == 0 || component.fields.size() > MaximumBehaviorFields)
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));

        std::unordered_set<std::string_view> names;
        names.reserve(component.fields.size());
        for (const BehaviorField &field : component.fields) {
            if (field.name.empty() || field.name.size() > MaximumBehaviorFieldNameBytes || !names.emplace(field.name).second)
                return Result<void>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));
            if (const auto *text = std::get_if<std::string>(&field.value); text && text->size() > 4096)
                return Result<void>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent));
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay
