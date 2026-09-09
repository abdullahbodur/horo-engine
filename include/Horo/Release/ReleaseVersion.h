#pragma once

/**
 * @file ReleaseVersion.h
 * @brief Canonical semantic release versions and side-effect-free version authority validation.
 */

#include "Horo/Application/ProjectVersion.h"
#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace Horo::Release {
    /** @brief Maximum canonical release SemVer text admitted by the release domain. */
    inline constexpr std::size_t MaximumReleaseVersionBytes = 128;
    /** @brief Maximum opaque source-revision bytes bound to one candidate. */
    inline constexpr std::size_t MaximumReleaseSourceRevisionBytes = 128;
    /** @brief One claim per supported authority source. */
    inline constexpr std::size_t MaximumReleaseVersionClaims = 5;

    /** @brief Full SemVer 2.0 value; build metadata participates in identity but not precedence. */
    struct ReleaseSemanticVersion final {
        std::uint32_t major{};
        std::uint32_t minor{};
        std::uint32_t patch{};
        std::string prerelease;
        std::string buildMetadata;

        /** @brief Reports whether this is a stable release. @return True when prerelease is empty. */
        [[nodiscard]] bool IsStable() const noexcept {
            return prerelease.empty();
        }

        bool operator==(const ReleaseSemanticVersion &) const noexcept = default;
    };

    /** @brief Strong release version for Horo Engine products and artifacts. */
    struct EngineProductVersion final {
        ReleaseSemanticVersion value;
        bool operator==(const EngineProductVersion &) const noexcept = default;
    };

    /** @brief Strong release version owned by a game project product. */
    struct GameProductVersion final {
        ReleaseSemanticVersion value;
        bool operator==(const GameProductVersion &) const noexcept = default;
    };

    /** @brief Explicit product-version alternatives; engine and game versions cannot be interchanged. */
    using ReleaseProductVersion = std::variant<EngineProductVersion, GameProductVersion>;

    /** @brief Sources whose pre-read version identities participate in release authority. */
    enum class ReleaseVersionClaimSource : std::uint8_t {
        Requested,
        Tag,
        Manifest,
        Notes,
        PersistentContract
    };

    /** @brief Claim value; persistent compatibility deliberately uses its build-metadata-free engine type. */
    using ReleaseVersionClaimValue = std::variant<EngineProductVersion, GameProductVersion, Application::EngineReleaseVersion>;

    /** @brief One typed version identity read by a boundary adapter before release build work begins. */
    struct ReleaseVersionClaim final {
        ReleaseVersionClaimSource source{ReleaseVersionClaimSource::Requested};
        ReleaseVersionClaimValue value;
        bool operator==(const ReleaseVersionClaim &) const noexcept = default;
    };

    /** @brief Bounded opaque source revision attached to the validated candidate identity. */
    struct ReleaseSourceRevision final {
        std::string value;
        bool operator==(const ReleaseSourceRevision &) const noexcept = default;
    };

    /** @brief Canonical product version and source revision accepted by every later release stage. */
    struct ReleaseVersionAuthority final {
        ReleaseProductVersion productVersion;
        ReleaseSourceRevision sourceRevision;
        bool operator==(const ReleaseVersionAuthority &) const noexcept = default;
    };

    /**
     * @brief Parses strict bounded SemVer 2.0 text without a tag prefix.
     * @param text Canonical version text.
     * @return Parsed full semantic version or a typed validation error.
     * @throws std::bad_alloc When storing identifiers or constructing an error fails.
     */
    [[nodiscard]] Result<ReleaseSemanticVersion> ParseReleaseVersion(std::string_view text);

    /**
     * @brief Parses an exact release tag in `v<canonical-semver>` form.
     * @param text Tag text including the required lowercase `v` prefix.
     * @return Parsed semantic version or a typed tag/version error.
     * @throws std::bad_alloc When storing identifiers or constructing an error fails.
     */
    [[nodiscard]] Result<ReleaseSemanticVersion> ParseReleaseTag(std::string_view text);

    /**
     * @brief Formats a valid full semantic version canonically.
     * @param version Valid parsed release version.
     * @return Canonical SemVer text without a tag prefix.
     * @throws std::bad_alloc When formatting output fails.
     */
    [[nodiscard]] std::string FormatReleaseVersion(const ReleaseSemanticVersion &version);

    /**
     * @brief Compares SemVer precedence while deliberately ignoring build metadata.
     * @param lhs Left version.
     * @param rhs Right version.
     * @return Strong ordering under SemVer precedence rules.
     */
    [[nodiscard]] std::strong_ordering CompareReleaseVersionPrecedence(const ReleaseSemanticVersion &lhs,
                                                                       const ReleaseSemanticVersion &rhs) noexcept;

    /**
     * @brief Resolves pre-read version claims to one canonical product/version/revision authority.
     * @param claims Bounded claims containing exactly one Requested identity and at most one of each other source.
     * @param sourceRevision Non-empty bounded opaque revision selected by the source adapter.
     * @return Validated authority or a typed invalid, conflict, product-kind, or compatibility error.
     * @throws std::bad_alloc When copying authority data or constructing an error fails.
     */
    [[nodiscard]] Result<ReleaseVersionAuthority> ValidateReleaseVersionAuthority(std::span<const ReleaseVersionClaim> claims,
                                                                                  const ReleaseSourceRevision &sourceRevision);
}  // namespace Horo::Release
