#pragma once

#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <string>
#include <string_view>

namespace Horo::Application
{
    /**
     * @file ProjectVersion.h
     * @brief Canonical Horo release versions and persistent-contract identities.
     */

    /** @brief Semantic Horo Engine version used by durable project metadata. */
    struct HoroVersion
    {
        std::uint32_t major{};
        std::uint32_t minor{};
        std::uint32_t patch{};
        std::string prerelease;

        [[nodiscard]] bool IsStable() const noexcept
        {
            return prerelease.empty();
        }

        [[nodiscard]] bool operator==(const HoroVersion&) const noexcept = default;
    };

    /** @brief Strong exact engine-release version. */
    struct EngineReleaseVersion
    {
        HoroVersion value;
        [[nodiscard]] bool operator==(const EngineReleaseVersion&) const noexcept = default;
    };

    /** @brief Strong version of the engine release that established a persistent contract. */
    struct ContractBaselineVersion
    {
        HoroVersion value;
        [[nodiscard]] bool operator==(const ContractBaselineVersion&) const noexcept = default;
    };

    /** @brief Canonical SHA-256 identity for a persistent project contract. */
    struct PersistentContractHash
    {
        std::array<std::uint8_t, 32> bytes{};
        [[nodiscard]] bool operator==(const PersistentContractHash&) const noexcept = default;
    };

    /** @brief Canonical SHA-256 identity for one release compatibility decision. */
    struct CompatibilityDecisionHash
    {
        std::array<std::uint8_t, 32> bytes{};
        [[nodiscard]] bool operator==(const CompatibilityDecisionHash&) const noexcept = default;
    };

    /** @brief Canonical SHA-256 hash of migration-controlled file content. */
    struct MigrationContentHash
    {
        std::array<std::uint8_t, 32> bytes{};
        [[nodiscard]] bool operator==(const MigrationContentHash&) const noexcept = default;
    };

    /** @brief Hash anchoring the canonical permanent migration-history document. */
    struct MigrationHistoryHead
    {
        MigrationContentHash content;
        [[nodiscard]] bool operator==(const MigrationHistoryHead&) const noexcept = default;
    };

    /**
     * @brief Parses canonical SemVer without build metadata.
     * @param text Version text, limited to 64 bytes.
     * @return Parsed version or a typed validation error.
     */
    [[nodiscard]] Result<HoroVersion> ParseHoroVersion(std::string_view text);

    /**
     * @brief Formats a parsed version in canonical SemVer form.
     * @param version Valid Horo version.
     * @return Canonical version text without build metadata.
     */
    [[nodiscard]] std::string FormatHoroVersion(const HoroVersion& version);

    /**
     * @brief Compares versions using SemVer precedence.
     * @param lhs Left version.
     * @param rhs Right version.
     * @return Strong ordering according to SemVer precedence.
     */
    [[nodiscard]] std::strong_ordering CompareHoroVersions(const HoroVersion& lhs, const HoroVersion& rhs) noexcept;

    /**
     * @brief Parses canonical `sha256:` plus 64 lowercase hexadecimal characters.
     * @param text Hash text.
     * @return Parsed hash or a typed validation error.
     */
    [[nodiscard]] Result<PersistentContractHash> ParsePersistentContractHash(std::string_view text);

    /**
     * @brief Formats a persistent-contract hash in canonical form.
     * @param hash Persistent contract hash.
     * @return Canonical lowercase SHA-256 identity.
     */
    [[nodiscard]] std::string FormatPersistentContractHash(const PersistentContractHash& hash);

    /**
     * @brief Parses a canonical compatibility-decision SHA-256 identity.
     * @param text Hash text.
     * @return Parsed hash or a typed validation error.
     */
    [[nodiscard]] Result<CompatibilityDecisionHash> ParseCompatibilityDecisionHash(std::string_view text);

    /**
     * @brief Formats a compatibility-decision hash in canonical form.
     * @param hash Compatibility decision hash.
     * @return Canonical lowercase SHA-256 identity.
     */
    [[nodiscard]] std::string FormatCompatibilityDecisionHash(const CompatibilityDecisionHash& hash);
} // namespace Horo::Application
