#include "GameAssetValidation.h"

#include "GameplayIdentityValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <array>
#include <functional>
#include <unordered_set>

namespace Horo::Gameplay::Detail {
    namespace {
        [[nodiscard]] bool IsExtension(const std::string_view value) noexcept {
            return !value.empty() && value.size() <= MaximumGameAssetExtensionBytes && std::ranges::all_of(value, [](const char character) {
                return IsAsciiLower(character) || IsAsciiDigit(character);
            });
        }

        [[nodiscard]] bool HasUniqueStrings(const std::span<const std::string> values) {
            std::unordered_set<std::string_view> unique;
            unique.reserve(values.size());
            return std::ranges::all_of(values, [&unique](const std::string &value) {
                return unique.emplace(value).second;
            });
        }

        [[nodiscard]] bool HasValidEditorMetadata(const GameAssetEditorRepresentation &editor) {
            const auto boundedText = [](const std::string &value) {
                return !value.empty() && value.size() <= MaximumGameAssetEditorTextBytes;
            };
            const std::array valid{boundedText(editor.displayName), boundedText(editor.category), boundedText(editor.iconName),
                                   editor.fields.size() <= MaximumGameAssetFields};
            if (!std::ranges::all_of(valid, std::identity{}))
                return false;
            std::unordered_set<std::string_view> ids;
            ids.reserve(editor.fields.size());
            return std::ranges::all_of(editor.fields, [&ids](const GameAssetEditorFieldDescriptor &field) {
                return field.id.IsValid() && !field.displayName.empty() && field.displayName.size() <= MaximumGameAssetEditorTextBytes &&
                       ids.emplace(field.id.Value()).second;
            });
        }

        [[nodiscard]] bool HasValidSourceExtensions(const GameAssetTypeDescriptor &descriptor) {
            return !descriptor.sourceExtensions.empty() && descriptor.sourceExtensions.size() <= MaximumGameAssetSourceExtensions &&
                   HasUniqueStrings(descriptor.sourceExtensions) && std::ranges::all_of(descriptor.sourceExtensions, IsExtension);
        }

        [[nodiscard]] bool HasValidCookTargets(const GameAssetTypeDescriptor &descriptor) {
            return !descriptor.cookTargets.empty() && descriptor.cookTargets.size() <= MaximumGameAssetCookTargets &&
                   !ContainsInvalidOrDuplicateIds(std::span<const AssetCookTargetId>{descriptor.cookTargets}) &&
                   std::ranges::all_of(descriptor.cookTargets, [](const AssetCookTargetId &target) {
                return target.Value().size() <= MaximumGameAssetCookTargetBytes;
            });
        }

        [[nodiscard]] bool HasCompleteHandlers(const GameAssetHandlerBinding &handler) noexcept {
            return handler.importAsset != nullptr && handler.serializeAsset != nullptr && handler.cookAsset != nullptr;
        }
    }  // namespace

    Result<void> ValidateGameAssetRegistration(const GameAssetTypeRegistration &registration, const std::string_view moduleId) {
        const GameAssetTypeDescriptor &descriptor = registration.descriptor;
        const std::array valid{descriptor.typeId.IsValid(),
                               BelongsToModule(descriptor.typeId.Value(), moduleId),
                               descriptor.schemaVersion != 0,
                               HasValidSourceExtensions(descriptor),
                               HasValidCookTargets(descriptor),
                               HasValidEditorMetadata(descriptor.editor),
                               HasCompleteHandlers(registration.handler)};
        if (!std::ranges::all_of(valid, std::identity{}))
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidGameAssetDescriptor));
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay::Detail
