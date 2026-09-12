#include "GameAssetValidation.h"

#include "GameplayIdentityValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <unordered_set>

namespace Horo::Gameplay::Detail {
    namespace {
        [[nodiscard]] bool BelongsToModule(const std::string_view value, const std::string_view moduleId) noexcept {
            return value.size() > moduleId.size() + 1 && value.starts_with(moduleId) && value[moduleId.size()] == '.';
        }

        [[nodiscard]] bool IsExtension(const std::string_view value) noexcept {
            return !value.empty() && value.size() <= MaximumGameAssetExtensionBytes && std::ranges::all_of(value, [](const char character) {
                return IsAsciiLower(character) || IsAsciiDigit(character);
            });
        }

        [[nodiscard]] bool IsCookTarget(const std::string_view value) noexcept {
            if (value.empty() || value.size() > MaximumGameAssetCookTargetBytes)
                return false;
            bool hasSeparator = false;
            std::size_t segmentStart = 0;
            for (std::size_t index = 0; index <= value.size(); ++index) {
                const bool atEnd = index == value.size();
                if (!atEnd && value[index] != '-')
                    continue;
                if (index == segmentStart || !IsAsciiLower(value[segmentStart]))
                    return false;
                if (!std::ranges::all_of(value.substr(segmentStart + 1, index - segmentStart - 1), [](const char character) {
                    return IsAsciiLower(character) || IsAsciiDigit(character);
                }))
                    return false;
                segmentStart = index + 1;
                hasSeparator = hasSeparator || !atEnd;
            }
            return hasSeparator;
        }

        [[nodiscard]] bool HasUniqueStrings(const std::span<const std::string> values) {
            std::unordered_set<std::string_view> unique;
            unique.reserve(values.size());
            return std::ranges::all_of(values, [&unique](const std::string &value) {
                return unique.emplace(value).second;
            });
        }

        [[nodiscard]] bool HasValidEditorMetadata(const GameAssetEditorRepresentation &editor) {
            if (editor.displayName.empty() || editor.displayName.size() > MaximumGameAssetEditorTextBytes || editor.category.empty() ||
                editor.category.size() > MaximumGameAssetEditorTextBytes || editor.iconName.empty() ||
                editor.iconName.size() > MaximumGameAssetEditorTextBytes || editor.fields.size() > MaximumGameAssetFields)
                return false;
            std::unordered_set<std::string_view> ids;
            ids.reserve(editor.fields.size());
            return std::ranges::all_of(editor.fields, [&ids](const GameAssetEditorFieldDescriptor &field) {
                return field.id.IsValid() && !field.displayName.empty() && field.displayName.size() <= MaximumGameAssetEditorTextBytes &&
                       ids.emplace(field.id.Value()).second;
            });
        }
    }  // namespace

    Result<void> ValidateGameAssetRegistration(const GameAssetTypeRegistration &registration, const std::string_view moduleId) {
        const GameAssetTypeDescriptor &descriptor = registration.descriptor;
        if (!descriptor.typeId.IsValid() || !BelongsToModule(descriptor.typeId.Value(), moduleId) || descriptor.schemaVersion == 0 ||
            descriptor.sourceExtensions.empty() || descriptor.sourceExtensions.size() > MaximumGameAssetSourceExtensions ||
            descriptor.cookTargets.empty() || descriptor.cookTargets.size() > MaximumGameAssetCookTargets ||
            registration.handler.importAsset == nullptr || registration.handler.serializeAsset == nullptr ||
            registration.handler.cookAsset == nullptr || !HasValidEditorMetadata(descriptor.editor) ||
            !HasUniqueStrings(descriptor.sourceExtensions) || !HasUniqueStrings(descriptor.cookTargets) ||
            !std::ranges::all_of(descriptor.sourceExtensions, IsExtension) || !std::ranges::all_of(descriptor.cookTargets, IsCookTarget))
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidGameAssetDescriptor));
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay::Detail
