/**
 * @file AssetIdentity.cpp
 * @brief GUID generation and display-name derivation for editor asset definitions.
 *
 * GUIDs are 32 lowercase hexadecimal digits (128 random bits from @c std::random_device).
 * Display names prefer a non-empty trimmed @c AssetDef::displayName, then the logical
 * asset ID, then the imported mesh path stem, falling back to @c "Asset".
 */
#include "ui/editor/AssetIdentity.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <random>
#include <string>

namespace Horo::Editor
{
    namespace
    {
        /** @brief Removes leading and trailing ASCII whitespace from @p text. */
        std::string Trim(std::string text)
        {
            const auto first = std::ranges::find_if_not(
                text, [](unsigned char c) { return std::isspace(c) != 0; });
            const auto last =
                std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c)
                {
                    return std::isspace(c) != 0;
                }).base();
            if (first >= last)
                return {};
            return std::string(first, last);
        }
    } // namespace

    /** @copydoc GenerateAssetGuid */
    std::string GenerateAssetGuid()
    {
        static constexpr std::array<char, 16> kHex = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
        };

        std::random_device rd;
        std::array<std::byte, 16> randomBytes{};
        for (std::byte& byte : randomBytes)
            byte = static_cast<std::byte>(rd());

        std::string guid;
        guid.reserve(32);
        for (const std::byte byte : randomBytes)
        {
            const std::byte highNibble = (byte >> 4) & std::byte{0x0f};
            const std::byte lowNibble = byte & std::byte{0x0f};
            guid.push_back(kHex[std::to_integer<size_t>(highNibble)]);
            guid.push_back(kHex[std::to_integer<size_t>(lowNibble)]);
        }
        return guid;
    }

    /** @copydoc MakeAssetDisplayName */
    std::string MakeAssetDisplayName(const std::string& assetId,
                                     const AssetDef& asset)
    {
        if (const std::string trimmedDisplay = Trim(asset.displayName);
            !trimmedDisplay.empty())
            return trimmedDisplay;

        if (const std::string trimmedAssetId = Trim(assetId); !trimmedAssetId.empty())
            return trimmedAssetId;

        if (!asset.mesh.empty())
        {
            const std::filesystem::path meshPath(asset.mesh);
            const std::string stem = Trim(meshPath.stem().string());
            if (!stem.empty())
                return stem;
        }

        return "Asset";
    }

    /** @copydoc EnsureAssetIdentity(const std::string &, AssetDef *) */
    void EnsureAssetIdentity(const std::string& assetId, AssetDef* asset)
    {
        if (!asset)
            return;
        if (asset->guid.empty())
            asset->guid = GenerateAssetGuid();
        asset->displayName = MakeAssetDisplayName(assetId, *asset);
    }

    /** @copydoc EnsureAssetIdentity(SceneDocument *) */
    void EnsureAssetIdentity(SceneDocument* doc)
    {
        if (!doc)
            return;
        for (auto& [assetId, assetDef] : doc->assets)
            EnsureAssetIdentity(assetId, &assetDef);
    }
} // namespace Horo::Editor
