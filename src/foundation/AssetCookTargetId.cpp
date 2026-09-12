#include "Horo/Foundation/AssetCookTargetId.h"

#include <algorithm>

namespace Horo {
    namespace {
        const ErrorDomainId AssetCookTargetDomain{"horo.asset"};

        [[nodiscard]] bool IsLowerAlpha(const char character) noexcept {
            return character >= 'a' && character <= 'z';
        }

        [[nodiscard]] bool IsSegmentCharacter(const char character) noexcept {
            return IsLowerAlpha(character) || (character >= '0' && character <= '9');
        }

        [[nodiscard]] bool IsCanonicalTarget(const std::string_view text) noexcept {
            if (text.empty() || text.size() > MaximumAssetCookTargetIdBytes)
                return false;
            bool hasSeparator = false;
            std::size_t segmentStart = 0;
            for (std::size_t index = 0; index <= text.size(); ++index) {
                const bool atEnd = index == text.size();
                if (!atEnd && text[index] != '-')
                    continue;
                if (index == segmentStart || !IsLowerAlpha(text[segmentStart]) ||
                    !std::ranges::all_of(text.substr(segmentStart + 1, index - segmentStart - 1), IsSegmentCharacter))
                    return false;
                segmentStart = index + 1;
                hasSeparator = hasSeparator || !atEnd;
            }
            return hasSeparator;
        }
    }  // namespace

    namespace AssetCookTargetErrors {
        const ErrorCodeDescriptor Invalid{
            .domain = AssetCookTargetDomain,
            .code = ErrorCode{"asset.cook.invalid_target"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Cook target ID is not canonical.",
            .remediationHint = "Use a lowercase hyphen-separated identifier such as headless-null.",
        };
    }

    /** @copydoc AssetCookTargetId::Parse */
    Result<AssetCookTargetId> AssetCookTargetId::Parse(const std::string_view text) {
        if (!IsCanonicalTarget(text))
            return Result<AssetCookTargetId>::Failure(MakeError(AssetCookTargetErrors::Invalid));
        return Result<AssetCookTargetId>::Success(AssetCookTargetId{std::string{text}});
    }

    /** @copydoc AssetCookTargetId::Value */
    const std::string &AssetCookTargetId::Value() const noexcept {
        return value_;
    }

    /** @copydoc AssetCookTargetId::IsValid */
    bool AssetCookTargetId::IsValid() const noexcept {
        return !value_.empty();
    }
}  // namespace Horo
