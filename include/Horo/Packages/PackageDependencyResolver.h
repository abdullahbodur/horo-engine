#pragma once

/**
 * @file PackageDependencyResolver.h
 * @brief Deterministic bounded package dependency resolution contract.
 */

#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Packages {
    /** @brief Canonical package identity used by dependency resolution. */
    class HoroPackageId {
    public:
        /**
         * @brief Parses a bounded lowercase reverse-domain-style package identity.
         * @param text Candidate canonical identity.
         * @return Parsed identity or a typed invalid-input failure.
         */
        [[nodiscard]] static Result<HoroPackageId> Parse(std::string_view text);
        /**
         * @brief Returns the canonical identity.
         * @return Stable identity storage owned by this value.
         */
        [[nodiscard]] const std::string &Value() const noexcept;
        [[nodiscard]] auto operator<=>(const HoroPackageId &) const = default;

    private:
        explicit HoroPackageId(std::string value);
        std::string value_;
    };

    /** @brief Parsed semantic version with deterministic precedence. */
    struct PackageVersion {
        std::uint32_t major{};
        std::uint32_t minor{};
        std::uint32_t patch{};
        std::string prerelease;

        /**
         * @brief Parses canonical `major.minor.patch` with an optional prerelease suffix.
         * @param text Candidate semantic-version spelling.
         * @return Parsed version or a typed invalid-input failure.
         */
        [[nodiscard]] static Result<PackageVersion> Parse(std::string_view text);
        /**
         * @brief Returns the canonical semantic-version spelling.
         * @return Canonical version text.
         */
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] std::strong_ordering operator<=>(const PackageVersion &other) const noexcept;
        [[nodiscard]] bool operator==(const PackageVersion &other) const noexcept = default;
    };

    /** @brief Closed semantic-version constraint supported by the initial resolver. */
    struct PackageVersionRange {
        enum class Kind : std::uint8_t {
            Any,
            Exact,
            Caret,
        };

        Kind kind{Kind::Any};
        PackageVersion version;

        /**
         * @brief Reports whether a version satisfies this range.
         * @param candidate Version to test.
         * @return True when the candidate is permitted.
         */
        [[nodiscard]] bool Allows(const PackageVersion &candidate) const noexcept;
        /**
         * @brief Returns a deterministic diagnostic spelling.
         * @return Canonical range text.
         */
        [[nodiscard]] std::string ToString() const;
    };

    /** @brief Required or opt-in dependency edge. */
    enum class PackageDependencyRequirement : std::uint8_t {
        Required,
        Optional,
    };

    /** @brief Portable dependency intent consumed by the resolver. */
    struct PackageDependencyRequest {
        HoroPackageId package;
        PackageVersionRange versions;
        PackageDependencyRequirement requirement{PackageDependencyRequirement::Required};
        std::vector<std::string> requiredFeatures;
    };

    /** @brief Exact host compatibility tuple; empty candidate lists mean portable. */
    struct PackagePlatform {
        std::string operatingSystem;
        std::string architecture;
        std::string sdkAbi;
        [[nodiscard]] bool operator==(const PackagePlatform &) const noexcept = default;
    };

    /** @brief One immutable version advertised by one pre-ranked source snapshot. */
    struct PackageResolutionCandidate {
        HoroPackageId package;
        PackageVersion version;
        std::string sourceId;
        std::uint32_t sourceRank{}; /**< Lower ranks have higher explicit policy precedence. */
        std::string artifactDigest;
        std::vector<std::string> features;
        std::vector<PackageDependencyRequest> dependencies;
        std::vector<PackagePlatform> platforms;
        bool yanked{false};
    };

    /** @brief Resource ceilings checked before graph exploration. */
    struct PackageResolverLimits {
        std::size_t candidates{4096};
        std::size_t packages{512};
        std::size_t dependenciesPerCandidate{128};
        std::size_t featuresPerCandidate{128};
        std::size_t graphDepth{128};
    };

    /** @brief Complete immutable input snapshot for one deterministic resolution. */
    struct PackageResolutionRequest {
        std::vector<PackageDependencyRequest> roots;
        std::vector<PackageResolutionCandidate> candidates;
        PackagePlatform host;
        bool includeOptionalDependencies{false};
        PackageResolverLimits limits;
    };

    /** @brief Exact selected package identity for later lockfile generation. */
    struct ResolvedPackage {
        HoroPackageId package;
        PackageVersion version;
        std::string sourceId;
        std::string artifactDigest;
        std::vector<HoroPackageId> dependencies;
    };

    /** @brief Canonically package-ID-sorted deterministic resolution plan. */
    struct PackageResolutionPlan {
        std::vector<ResolvedPackage> packages;
    };

    /** @brief Pure bounded dependency resolver with no I/O, cache, lockfile, or activation side effects. */
    class PackageDependencyResolver final {
    public:
        /**
         * @brief Resolves required roots and enabled optional edges from one complete source snapshot.
         * @param request Owned immutable-by-convention requests, candidates, host tuple and limits.
         * @return Canonical exact plan or a stable typed resolver failure.
         */
        [[nodiscard]] static Result<PackageResolutionPlan> Resolve(const PackageResolutionRequest &request);
    };
}  // namespace Horo::Packages
