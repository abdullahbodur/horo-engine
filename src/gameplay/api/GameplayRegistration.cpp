#include "Horo/Gameplay/GameplayRegistration.h"

#include "GameplayIdentityValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

namespace Horo::Gameplay {
    /** @copydoc GameplaySystemId::Parse */
    Result<GameplaySystemId> GameplaySystemId::Parse(const std::string_view value) {
        if (!Detail::IsNamespacedGameplayId(value, MaximumGameplayRegistrationIdBytes))
            return Result<GameplaySystemId>::Failure(MakeError(GameplayErrors::InvalidSystemId));
        return Result<GameplaySystemId>::Success(GameplaySystemId{std::string{value}});
    }

    /** @copydoc GameplaySystemId::Value */
    const std::string &GameplaySystemId::Value() const noexcept {
        return value_;
    }

    /** @copydoc GameplaySystemId::IsValid */
    bool GameplaySystemId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc GameplayServiceId::Parse */
    Result<GameplayServiceId> GameplayServiceId::Parse(const std::string_view value) {
        if (!Detail::IsNamespacedGameplayId(value, MaximumGameplayRegistrationIdBytes))
            return Result<GameplayServiceId>::Failure(MakeError(GameplayErrors::InvalidServiceId));
        return Result<GameplayServiceId>::Success(GameplayServiceId{std::string{value}});
    }

    /** @copydoc GameplayServiceId::Value */
    const std::string &GameplayServiceId::Value() const noexcept {
        return value_;
    }

    /** @copydoc GameplayServiceId::IsValid */
    bool GameplayServiceId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc GameplayCapabilityId::Parse */
    Result<GameplayCapabilityId> GameplayCapabilityId::Parse(const std::string_view value) {
        if (!Detail::IsNamespacedId(value, MaximumGameplayRegistrationIdBytes))
            return Result<GameplayCapabilityId>::Failure(MakeError(GameplayErrors::InvalidCapabilityId));
        return Result<GameplayCapabilityId>::Success(GameplayCapabilityId{std::string{value}});
    }

    /** @copydoc GameplayCapabilityId::Value */
    const std::string &GameplayCapabilityId::Value() const noexcept {
        return value_;
    }

    /** @copydoc GameplayCapabilityId::IsValid */
    bool GameplayCapabilityId::IsValid() const noexcept {
        return !value_.empty();
    }
}  // namespace Horo::Gameplay
