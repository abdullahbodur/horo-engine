#pragma once

/**
 * @file ReleaseProfile.h
 * @brief Typed persistent release-profile presets and immutable resolution.
 */

#include "Horo/Release/DistributionModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Release {
    /** @brief Admission and inheritance ceilings for untrusted profile catalogs. */
    struct ReleaseProfileLimits final {
        std::size_t documentBytes{1024U * 1024U};
        std::size_t presets{128};
        std::size_t inheritanceDepth{32};
        std::size_t destinationsPerPreset{64};
        std::size_t capabilitiesPerPreset{64};
        std::size_t availableCapabilities{256}; /**< Maximum host capability identities admitted during one resolution. */
        std::size_t identityBytes{128};         /**< Configurable ceiling at or below the distribution identity hard limit. */
    };

    /** @brief Stable portable name of a project release preset. */
    struct ReleaseProfileId final {
        std::string value;
        bool operator==(const ReleaseProfileId &) const noexcept = default;
    };

    /** @brief Stable public identity of a publication destination class. */
    struct ReleaseDestinationId final {
        std::string value;
        bool operator==(const ReleaseDestinationId &) const noexcept = default;
    };

    /** @brief Stable public identity of a host capability required by a profile. */
    struct ReleaseCapabilityId final {
        std::string value;
        bool operator==(const ReleaseCapabilityId &) const noexcept = default;
    };

    /** @brief Runtime asset packaging and chunking policy. */
    enum class ReleaseAssetPolicy : std::uint8_t {
        Omit,
        SinglePackage,
        Chunked
    };

    /** @brief Product content selected independently from build-toolchain configuration. */
    struct ReleaseContentPolicy final {
        bool executables{};
        bool runtimeLibraries{};
        ReleaseAssetPolicy assets{ReleaseAssetPolicy::Omit};
        bool developerDiagnostics{};
        bool crashReports{};
        bool operator==(const ReleaseContentPolicy &) const noexcept = default;
    };

    /** @brief Supplemental symbol and crash-report artifact policy. */
    enum class ReleaseSymbolPolicy : std::uint8_t {
        Omit,
        SeparateArtifact,
        RequiredSeparateArtifact
    };

    /** @brief Signing requirement without credential identity or provider binding. */
    enum class ReleaseSigningPolicy : std::uint8_t {
        Disabled,
        WhenSupported,
        Required
    };

    /** @brief Optional fields supplied by one named preset and inherited explicitly. */
    struct ReleaseProfilePreset final {
        ReleaseProfileId id;
        std::optional<ReleaseProfileId> parent;
        std::optional<DistributionProductIdentity> product;
        std::optional<DistributionArtifactClass> artifactClass;
        std::optional<DistributionPlatform> platform;
        std::optional<DistributionPackageFormat> packageFormat;
        std::optional<ReleaseContentPolicy> content;
        std::optional<ReleaseSymbolPolicy> symbols;
        std::optional<ReleaseSigningPolicy> signing;
        std::optional<bool> notarizationRequired;
        std::optional<bool> includeLicensesAndNotices;
        std::optional<bool> includeReleaseNotes;
        std::optional<bool> updateEligible;
        std::optional<bool> patchEligible;
        std::optional<std::vector<ReleaseDestinationId>> eligibleDestinations;
        std::optional<std::vector<ReleaseCapabilityId>> requiredCapabilities;
    };

    /** @brief Fully resolved immutable product-release intent. */
    class EffectiveReleaseProfile final {
    public:
        /** @brief Returns the selected preset. @return Stable portable preset identity. */
        [[nodiscard]] const ReleaseProfileId &Id() const noexcept;
        /** @brief Returns the product identity. @return Backend-neutral distribution product. */
        [[nodiscard]] const DistributionProductIdentity &Product() const noexcept;
        /** @brief Returns the target platform. @return Explicit target operating-system family. */
        [[nodiscard]] DistributionPlatform Platform() const noexcept;
        /** @brief Returns the output artifact class. @return Installable, symbol, or diagnostics class. */
        [[nodiscard]] DistributionArtifactClass ArtifactClass() const noexcept;
        /** @brief Returns the package format. @return Explicit format, never inferred from platform. */
        [[nodiscard]] DistributionPackageFormat PackageFormat() const noexcept;
        /** @brief Returns included product content. @return Frozen content policy. */
        [[nodiscard]] const ReleaseContentPolicy &Content() const noexcept;
        /** @brief Returns the symbol policy. @return Frozen supplemental-artifact requirement. */
        [[nodiscard]] ReleaseSymbolPolicy Symbols() const noexcept;
        /** @brief Returns the signing policy. @return Frozen signing requirement. */
        [[nodiscard]] ReleaseSigningPolicy Signing() const noexcept;
        /** @brief Reports whether notarization is mandatory. @return Frozen notarization requirement. */
        [[nodiscard]] bool NotarizationRequired() const noexcept;
        /** @brief Reports whether licenses and notices are included. @return Frozen notice policy. */
        [[nodiscard]] bool IncludesLicensesAndNotices() const noexcept;
        /** @brief Reports whether release notes are included. @return Frozen notes policy. */
        [[nodiscard]] bool IncludesReleaseNotes() const noexcept;
        /** @brief Reports update eligibility. @return Frozen update policy. */
        [[nodiscard]] bool UpdateEligible() const noexcept;
        /** @brief Reports patch eligibility. @return Frozen patch policy. */
        [[nodiscard]] bool PatchEligible() const noexcept;
        /** @brief Returns public destination eligibility. @return Sorted stable destination identities. */
        [[nodiscard]] std::span<const ReleaseDestinationId> EligibleDestinations() const noexcept;
        /** @brief Returns required host capabilities. @return Sorted stable public capability identities. */
        [[nodiscard]] std::span<const ReleaseCapabilityId> RequiredCapabilities() const noexcept;
        /** @brief Serializes the unique effective-profile representation. @return Canonical UTF-8 JSON ending in a newline. */
        [[nodiscard]] std::string SerializeCanonical() const;

    private:
        friend class ReleaseProfileCatalog;
        EffectiveReleaseProfile(ReleaseProfileId id, ReleaseProfilePreset resolved);

        ReleaseProfileId m_id;
        DistributionProductIdentity m_product;
        DistributionArtifactClass m_artifactClass;
        DistributionPlatform m_platform;
        DistributionPackageFormat m_packageFormat;
        ReleaseContentPolicy m_content;
        ReleaseSymbolPolicy m_symbols;
        ReleaseSigningPolicy m_signing;
        bool m_notarizationRequired{};
        bool m_includeLicensesAndNotices{};
        bool m_includeReleaseNotes{};
        bool m_updateEligible{};
        bool m_patchEligible{};
        std::vector<ReleaseDestinationId> m_destinations;
        std::vector<ReleaseCapabilityId> m_capabilities;
    };

    /** @brief Validated named preset catalog with deterministic single-parent resolution. */
    class ReleaseProfileCatalog final {
    public:
        /**
         * @brief Creates a catalog from typed presets and canonicalizes their order.
         * @param presets Complete named preset set.
         * @param limits Admission and inheritance ceilings.
         * @return Validated catalog or a typed profile error.
         */
        [[nodiscard]] static Result<ReleaseProfileCatalog> Create(std::vector<ReleaseProfilePreset> presets,
                                                                  const ReleaseProfileLimits &limits = {});
        /**
         * @brief Parses a strict schema-v1 catalog with duplicate-key and exact-field validation.
         * @param json Complete catalog bytes.
         * @param limits Admission and inheritance ceilings.
         * @return Validated catalog or a typed profile error; parsing performs no I/O.
         */
        [[nodiscard]] static Result<ReleaseProfileCatalog> Parse(std::string_view json, const ReleaseProfileLimits &limits = {});
        /**
         * @brief Resolves one preset into immutable effective policy.
         * @param id Named preset to resolve.
         * @param availableCapabilities Stable public capabilities supported by the host.
         * @return Effective profile or an actionable missing-parent, cycle, conflict, or capability failure.
         */
        [[nodiscard]] Result<EffectiveReleaseProfile> Resolve(ReleaseProfileId id,
                                                              std::span<const ReleaseCapabilityId> availableCapabilities) const;
        /** @brief Serializes named presets in canonical identity order. @return Canonical UTF-8 schema-v1 JSON ending in a newline. */
        [[nodiscard]] std::string SerializeCanonical() const;
        /** @brief Returns all presets in canonical identity order. @return Borrowed immutable preset view. */
        [[nodiscard]] std::span<const ReleaseProfilePreset> Presets() const noexcept;

    private:
        ReleaseProfileCatalog(std::vector<ReleaseProfilePreset> presets, const ReleaseProfileLimits &limits);
        std::vector<ReleaseProfilePreset> m_presets;
        ReleaseProfileLimits m_limits;
    };
}  // namespace Horo::Release
