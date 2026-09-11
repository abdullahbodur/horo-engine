#include "Horo/WorldStreaming/WorldPartitionCapabilityProfile.h"

namespace Horo::WorldStreaming {
    namespace {
        constexpr std::uint8_t AllPackageBits = static_cast<std::uint8_t>(WorldPartitionPackageCapabilities::StandaloneCellFile) |
                                                static_cast<std::uint8_t>(WorldPartitionPackageCapabilities::ArchiveChunk);

        [[nodiscard]] constexpr bool IsKnown(const WorldPartitionProjectProfile value) noexcept {
            using enum WorldPartitionProjectProfile;
            return value < Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const WorldPartitionPrecision value) noexcept {
            using enum WorldPartitionPrecision;
            return value < Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const WorldPartitionPackageMode value) noexcept {
            using enum WorldPartitionPackageMode;
            return value < Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const WorldPartitionSettingsLifecycle value) noexcept {
            using enum WorldPartitionSettingsLifecycle;
            return value < Count;
        }

        [[nodiscard]] constexpr bool IsValid(const WorldPartitionPackageCapabilities value) noexcept {
            const auto bits = static_cast<std::uint8_t>(value);
            return bits != 0 && (bits & static_cast<std::uint8_t>(~AllPackageBits)) == 0;
        }

        [[nodiscard]] constexpr WorldPartitionPackageCapabilities CapabilityFor(const WorldPartitionPackageMode mode) noexcept {
            using enum WorldPartitionPackageMode;
            switch (mode) {
                case StandaloneCellFile:
                    return WorldPartitionPackageCapabilities::StandaloneCellFile;
                case ArchiveChunk:
                    return WorldPartitionPackageCapabilities::ArchiveChunk;
                case Count:
                    return WorldPartitionPackageCapabilities::None;
            }
            return WorldPartitionPackageCapabilities::None;
        }

        [[nodiscard]] constexpr bool Contains(const WorldPartitionPackageCapabilities set,
                                              const WorldPartitionPackageCapabilities capability) noexcept {
            return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(capability)) != 0;
        }

        [[nodiscard]] bool ValidCapabilityGrid(const WorldPartitionCapabilitySnapshot &capabilities) noexcept {
            return capabilities.minimumCellSizeMillimeters > 0 &&
                   capabilities.maximumCellSizeMillimeters >= capabilities.minimumCellSizeMillimeters &&
                   capabilities.maximumCellSizeMillimeters <= WorldPartitionCapabilitySnapshot::ImplementationMaximumCellSizeMillimeters &&
                   capabilities.maximumLodLevels > 0 && capabilities.maximumLodLevels <= 32;
        }

        [[nodiscard]] bool ValidCapabilityLimits(const WorldPartitionCapabilitySnapshot &capabilities) noexcept {
            return capabilities.maximumLayers > 0 && capabilities.maximumCells > 0 && capabilities.maximumLayerNameBytes > 0 &&
                   capabilities.maximumQueryResults > 0 && capabilities.maximumQueryResults <= capabilities.maximumCells;
        }

        [[nodiscard]] bool WithinImplementationLimits(const WorldPartitionCapabilitySnapshot &capabilities) noexcept {
            return capabilities.maximumLayers <= WorldPartitionCapabilitySnapshot::ImplementationMaximumLayers &&
                   capabilities.maximumCells <= WorldPartitionCapabilitySnapshot::ImplementationMaximumCells &&
                   capabilities.maximumLayerNameBytes <= WorldPartitionCapabilitySnapshot::ImplementationMaximumLayerNameBytes &&
                   capabilities.maximumQueryResults <= WorldPartitionCapabilitySnapshot::ImplementationMaximumQueryResults;
        }

        [[nodiscard]] bool ValidCapabilities(const WorldPartitionCapabilitySnapshot &capabilities) noexcept {
            return capabilities.capability.IsValid() && capabilities.revision.IsValid() && ValidCapabilityGrid(capabilities) &&
                   ValidCapabilityLimits(capabilities) && WithinImplementationLimits(capabilities) && IsKnown(capabilities.precision) &&
                   IsValid(capabilities.packages);
        }

        [[nodiscard]] bool ValidRequestGrid(const WorldPartitionProjectSettingsRequest &request) noexcept {
            return request.baseCellSizeMillimeters > 0 && request.lodLevels > 0 && request.lodLevels <= 32;
        }

        [[nodiscard]] bool ValidRequestLimits(const WorldPartitionProjectSettingsRequest &request) noexcept {
            return request.maximumLayers > 0 && request.maximumCells > 0 && request.maximumLayerNameBytes > 0 &&
                   request.maximumQueryResults > 0 && request.maximumQueryResults <= request.maximumCells;
        }

        [[nodiscard]] bool ValidRequest(const WorldPartitionProjectSettingsRequest &request) noexcept {
            return request.contractVersion == WorldPartitionProjectSettingsRequest::CurrentContractVersion && request.revision.IsValid() &&
                   request.settings.IsValid() && IsKnown(request.profile) && ValidRequestGrid(request) && ValidRequestLimits(request) &&
                   IsKnown(request.precision) && IsKnown(request.packageMode);
        }

        [[nodiscard]] bool ExceedsGridCapabilities(const WorldPartitionProjectSettingsRequest &request,
                                                   const WorldPartitionCapabilitySnapshot &capabilities) noexcept {
            return request.baseCellSizeMillimeters < capabilities.minimumCellSizeMillimeters ||
                   request.baseCellSizeMillimeters > capabilities.maximumCellSizeMillimeters ||
                   request.lodLevels > capabilities.maximumLodLevels;
        }

        [[nodiscard]] bool ExceedsStorageCapabilities(const WorldPartitionProjectSettingsRequest &request,
                                                      const WorldPartitionCapabilitySnapshot &capabilities) noexcept {
            return request.maximumLayers > capabilities.maximumLayers || request.maximumCells > capabilities.maximumCells ||
                   request.maximumLayerNameBytes > capabilities.maximumLayerNameBytes ||
                   request.maximumQueryResults > capabilities.maximumQueryResults;
        }
    }  // namespace

    /** @copydoc GetWorldPartitionProjectProfilePolicy */
    Result<WorldPartitionProjectProfilePolicy> GetWorldPartitionProjectProfilePolicy(const WorldPartitionProjectProfile profile) {
        using enum WorldPartitionPackageCapabilities;
        using enum WorldPartitionProjectProfile;
        switch (profile) {
            case Editor:
                return Result<WorldPartitionProjectProfilePolicy>::Success({profile, StandaloneCellFile | ArchiveChunk});
            case Standalone:
            case Client:
            case Server:
                return Result<WorldPartitionProjectProfilePolicy>::Success({profile, ArchiveChunk});
            case Count:
                break;
        }
        return Result<WorldPartitionProjectProfilePolicy>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsInvalid));
    }

    /** @copydoc WorldPartitionProjectSettings::Create */
    Result<WorldPartitionProjectSettings> WorldPartitionProjectSettings::Create(const WorldPartitionProjectSettingsRequest &request,
                                                                                const WorldPartitionCapabilitySnapshot &capabilities) {
        if (!ValidRequest(request) || !ValidCapabilities(capabilities))
            return Result<WorldPartitionProjectSettings>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsInvalid));
        if (request.precision != capabilities.precision)
            return Result<WorldPartitionProjectSettings>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsUnsupported));

        auto policy = GetWorldPartitionProjectProfilePolicy(request.profile);
        if (policy.HasError())
            return Result<WorldPartitionProjectSettings>::Failure(policy.ErrorValue());
        const auto requestedPackage = CapabilityFor(request.packageMode);
        if (!Contains(policy.Value().packages, requestedPackage) || !Contains(capabilities.packages, requestedPackage))
            return Result<WorldPartitionProjectSettings>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsUnsupported));

        if (ExceedsGridCapabilities(request, capabilities) || ExceedsStorageCapabilities(request, capabilities))
            return Result<WorldPartitionProjectSettings>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsCapacityExceeded));

        return Result<WorldPartitionProjectSettings>::Success(
            WorldPartitionProjectSettings{request, capabilities.capability, capabilities.revision});
    }

    WorldPartitionProjectSettings::WorldPartitionProjectSettings(const WorldPartitionProjectSettingsRequest &request,
                                                                 const WorldPartitionCapabilityId capability,
                                                                 const WorldPartitionCapabilityRevision capabilityRevision) noexcept
        : settings_(request.settings), revision_(request.revision), capability_(capability), capabilityRevision_(capabilityRevision),
          profile_(request.profile), baseCellSizeMillimeters_(request.baseCellSizeMillimeters), lodLevels_(request.lodLevels),
          maximumLayers_(request.maximumLayers), maximumCells_(request.maximumCells), maximumLayerNameBytes_(request.maximumLayerNameBytes),
          maximumQueryResults_(request.maximumQueryResults), precision_(request.precision), packageMode_(request.packageMode) {}

    /** @copydoc WorldPartitionProjectSettings::Revision */
    WorldPartitionSettingsRevision WorldPartitionProjectSettings::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc WorldPartitionProjectSettings::Settings */
    WorldPartitionSettingsId WorldPartitionProjectSettings::Settings() const noexcept {
        return settings_;
    }

    /** @copydoc WorldPartitionProjectSettings::Capability */
    WorldPartitionCapabilityId WorldPartitionProjectSettings::Capability() const noexcept {
        return capability_;
    }

    /** @copydoc WorldPartitionProjectSettings::CapabilityRevision */
    WorldPartitionCapabilityRevision WorldPartitionProjectSettings::CapabilityRevision() const noexcept {
        return capabilityRevision_;
    }

    /** @copydoc WorldPartitionProjectSettings::Profile */
    WorldPartitionProjectProfile WorldPartitionProjectSettings::Profile() const noexcept {
        return profile_;
    }

    /** @copydoc WorldPartitionProjectSettings::BaseCellSizeMillimeters */
    std::int64_t WorldPartitionProjectSettings::BaseCellSizeMillimeters() const noexcept {
        return baseCellSizeMillimeters_;
    }

    /** @copydoc WorldPartitionProjectSettings::LodLevels */
    std::uint8_t WorldPartitionProjectSettings::LodLevels() const noexcept {
        return lodLevels_;
    }

    /** @copydoc WorldPartitionProjectSettings::MaximumLayers */
    std::uint32_t WorldPartitionProjectSettings::MaximumLayers() const noexcept {
        return maximumLayers_;
    }

    /** @copydoc WorldPartitionProjectSettings::MaximumCells */
    std::uint32_t WorldPartitionProjectSettings::MaximumCells() const noexcept {
        return maximumCells_;
    }

    /** @copydoc WorldPartitionProjectSettings::MaximumLayerNameBytes */
    std::uint32_t WorldPartitionProjectSettings::MaximumLayerNameBytes() const noexcept {
        return maximumLayerNameBytes_;
    }

    /** @copydoc WorldPartitionProjectSettings::MaximumQueryResults */
    std::uint32_t WorldPartitionProjectSettings::MaximumQueryResults() const noexcept {
        return maximumQueryResults_;
    }

    /** @copydoc WorldPartitionProjectSettings::Precision */
    WorldPartitionPrecision WorldPartitionProjectSettings::Precision() const noexcept {
        return precision_;
    }

    /** @copydoc WorldPartitionProjectSettings::PackageMode */
    WorldPartitionPackageMode WorldPartitionProjectSettings::PackageMode() const noexcept {
        return packageMode_;
    }

    /** @copydoc ValidateWorldPartitionSettingsAdmission */
    Result<void> ValidateWorldPartitionSettingsAdmission(const WorldPartitionProjectSettings &settings,
                                                         const WorldPartitionSettingsId currentSettings,
                                                         const WorldPartitionSettingsRevision currentSettingsRevision,
                                                         const WorldPartitionCapabilityId currentCapability,
                                                         const WorldPartitionCapabilityRevision currentCapabilityRevision,
                                                         const WorldPartitionSettingsLifecycle lifecycle) {
        using enum WorldPartitionSettingsLifecycle;
        if (!currentSettings.IsValid() || !currentSettingsRevision.IsValid() || !currentCapability.IsValid() ||
            !currentCapabilityRevision.IsValid() || !IsKnown(lifecycle))
            return Result<void>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsInvalid));
        if (settings.Settings() != currentSettings || settings.Revision() != currentSettingsRevision ||
            settings.Capability() != currentCapability || settings.CapabilityRevision() != currentCapabilityRevision)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsStale));
        if (lifecycle == Cancelling || lifecycle == Closed)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::PartitionSettingsLifecycleUnavailable));
        return Result<void>::Success();
    }
}  // namespace Horo::WorldStreaming
