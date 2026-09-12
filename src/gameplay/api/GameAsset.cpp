#include "Horo/Gameplay/GameAsset.h"

#include "GameplayIdentityValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <array>
#include <functional>

namespace Horo::Gameplay {
    /** @copydoc GameAssetTypeId::Parse */
    Result<GameAssetTypeId> GameAssetTypeId::Parse(const std::string_view value) {
        if (!Detail::IsNamespacedGameplayId(value, MaximumGameAssetTypeIdBytes))
            return Result<GameAssetTypeId>::Failure(MakeError(GameplayErrors::InvalidGameAssetTypeId));
        return Result<GameAssetTypeId>::Success(GameAssetTypeId{std::string{value}});
    }

    /** @copydoc GameAssetTypeId::Value */
    const std::string &GameAssetTypeId::Value() const noexcept {
        return value_;
    }

    /** @copydoc GameAssetTypeId::IsValid */
    bool GameAssetTypeId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc GameAssetFieldId::Parse */
    Result<GameAssetFieldId> GameAssetFieldId::Parse(const std::string_view value) {
        if (!Detail::IsLowercaseIdentifier(value, MaximumGameAssetFieldIdBytes))
            return Result<GameAssetFieldId>::Failure(MakeError(GameplayErrors::InvalidGameAssetDescriptor));
        return Result<GameAssetFieldId>::Success(GameAssetFieldId{std::string{value}});
    }

    /** @copydoc GameAssetFieldId::Value */
    const std::string &GameAssetFieldId::Value() const noexcept {
        return value_;
    }

    /** @copydoc GameAssetFieldId::IsValid */
    bool GameAssetFieldId::IsValid() const noexcept {
        return !value_.empty();
    }

    /** @copydoc ValidateSerializedGameAsset */
    Result<void> ValidateSerializedGameAsset(const SerializedGameAsset &asset) {
        const std::array valid{asset.typeId.IsValid(), asset.schemaVersion != 0, asset.payload.size() <= MaximumGameAssetPayloadBytes,
                               asset.encoding == GameAssetPayloadEncoding::CanonicalJson ||
                                   asset.encoding == GameAssetPayloadEncoding::Binary};
        if (!std::ranges::all_of(valid, std::identity{}))
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidSerializedGameAsset));
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay
