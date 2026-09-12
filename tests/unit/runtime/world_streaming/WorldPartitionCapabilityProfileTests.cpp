#include "Horo/WorldStreaming/WorldPartitionCapabilityProfile.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string_view>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] T Id(const std::uint64_t value) {
            return T::Create(value).Value();
        }

        [[nodiscard]] WorldPartitionCapabilitySnapshot Capabilities() {
            return {
                .capability = Id<WorldPartitionCapabilityId>(5),
                .revision = Id<WorldPartitionCapabilityRevision>(7),
                .minimumCellSizeMillimeters = 1'000,
                .maximumCellSizeMillimeters = 1'024'000,
                .maximumLodLevels = 12,
                .maximumLayers = 64,
                .maximumCells = 100'000,
                .maximumLayerNameBytes = 8'192,
                .maximumQueryResults = 4'096,
                .precision = WorldPartitionPrecision::SignedMillimeter64,
                .packages = WorldPartitionPackageCapabilities::StandaloneCellFile | WorldPartitionPackageCapabilities::ArchiveChunk,
            };
        }

        [[nodiscard]] WorldPartitionProjectSettingsRequest Request() {
            return {
                .settings = Id<WorldPartitionSettingsId>(2),
                .revision = Id<WorldPartitionSettingsRevision>(3),
                .profile = WorldPartitionProjectProfile::Editor,
                .baseCellSizeMillimeters = 64'000,
                .lodLevels = 8,
                .maximumLayers = 16,
                .maximumCells = 50'000,
                .maximumLayerNameBytes = 4'096,
                .maximumQueryResults = 2'048,
                .precision = WorldPartitionPrecision::SignedMillimeter64,
                .packageMode = WorldPartitionPackageMode::StandaloneCellFile,
            };
        }

        [[nodiscard]] std::string_view ErrorCode(const Error &error) {
            return error.code.Value();
        }
    }  // namespace

    TEST_CASE("Partition settings capture one exact capability revision without fallback", "[unit][world_streaming][partition_settings]") {
        const auto result = WorldPartitionProjectSettings::Create(Request(), Capabilities());
        REQUIRE(result.HasValue());
        const auto &settings = result.Value();
        REQUIRE(settings.Settings() == Id<WorldPartitionSettingsId>(2));
        REQUIRE(settings.Revision() == Id<WorldPartitionSettingsRevision>(3));
        REQUIRE(settings.Capability() == Id<WorldPartitionCapabilityId>(5));
        REQUIRE(settings.CapabilityRevision() == Id<WorldPartitionCapabilityRevision>(7));
        REQUIRE(settings.Profile() == WorldPartitionProjectProfile::Editor);
        REQUIRE(settings.BaseCellSizeMillimeters() == 64'000);
        REQUIRE(settings.LodLevels() == 8);
        REQUIRE(settings.MaximumLayers() == 16);
        REQUIRE(settings.MaximumCells() == 50'000);
        REQUIRE(settings.MaximumLayerNameBytes() == 4'096);
        REQUIRE(settings.MaximumQueryResults() == 2'048);
        REQUIRE(settings.Precision() == WorldPartitionPrecision::SignedMillimeter64);
        REQUIRE(settings.PackageMode() == WorldPartitionPackageMode::StandaloneCellFile);
    }

    TEST_CASE("Partition profile policy is closed and packaged profiles require archive chunks",
              "[unit][world_streaming][partition_settings]") {
        const auto editor = GetWorldPartitionProjectProfilePolicy(WorldPartitionProjectProfile::Editor);
        REQUIRE(editor.HasValue());
        REQUIRE(editor.Value().packages ==
                (WorldPartitionPackageCapabilities::StandaloneCellFile | WorldPartitionPackageCapabilities::ArchiveChunk));
        for (const auto profile :
             {WorldPartitionProjectProfile::Standalone, WorldPartitionProjectProfile::Client, WorldPartitionProjectProfile::Server}) {
            const auto policy = GetWorldPartitionProjectProfilePolicy(profile);
            REQUIRE(policy.HasValue());
            REQUIRE(policy.Value().packages == WorldPartitionPackageCapabilities::ArchiveChunk);
        }
        REQUIRE(GetWorldPartitionProjectProfilePolicy(WorldPartitionProjectProfile::Count).HasError());
    }

    TEST_CASE("Partition settings reject malformed requests and capability snapshots transactionally",
              "[unit][world_streaming][partition_settings]") {
        auto request = Request();
        request.contractVersion = 0;
        REQUIRE(ErrorCode(WorldPartitionProjectSettings::Create(request, Capabilities()).ErrorValue()) ==
                WorldStreamingErrors::PartitionSettingsInvalid.code.Value());
        request = Request();
        request.settings = {};
        REQUIRE(WorldPartitionProjectSettings::Create(request, Capabilities()).HasError());
        request = Request();
        request.maximumQueryResults = request.maximumCells + 1;
        REQUIRE(WorldPartitionProjectSettings::Create(request, Capabilities()).HasError());
        request = Request();
        request.packageMode = WorldPartitionPackageMode::Count;
        REQUIRE(WorldPartitionProjectSettings::Create(request, Capabilities()).HasError());

        auto capabilities = Capabilities();
        capabilities.capability = {};
        REQUIRE(WorldPartitionProjectSettings::Create(Request(), capabilities).HasError());
        capabilities = Capabilities();
        capabilities.minimumCellSizeMillimeters = 0;
        REQUIRE(WorldPartitionProjectSettings::Create(Request(), capabilities).HasError());
        capabilities = Capabilities();
        capabilities.maximumLodLevels = 33;
        REQUIRE(WorldPartitionProjectSettings::Create(Request(), capabilities).HasError());
        capabilities = Capabilities();
        capabilities.maximumCells = WorldPartitionCapabilitySnapshot::ImplementationMaximumCells + 1;
        REQUIRE(WorldPartitionProjectSettings::Create(Request(), capabilities).HasError());
        capabilities = Capabilities();
        capabilities.packages = static_cast<WorldPartitionPackageCapabilities>(0x80);
        REQUIRE(WorldPartitionProjectSettings::Create(Request(), capabilities).HasError());
    }

    TEST_CASE("Partition settings reject every over-capacity boundary without clamping", "[unit][world_streaming][partition_settings]") {
        const auto capabilities = Capabilities();
        const auto expectCapacityFailure = [&](WorldPartitionProjectSettingsRequest request) {
            const auto result = WorldPartitionProjectSettings::Create(request, capabilities);
            REQUIRE(result.HasError());
            REQUIRE(ErrorCode(result.ErrorValue()) == WorldStreamingErrors::PartitionSettingsCapacityExceeded.code.Value());
        };

        auto request = Request();
        request.baseCellSizeMillimeters = capabilities.minimumCellSizeMillimeters - 1;
        expectCapacityFailure(request);
        request = Request();
        request.baseCellSizeMillimeters = capabilities.maximumCellSizeMillimeters + 1;
        expectCapacityFailure(request);
        request = Request();
        request.lodLevels = capabilities.maximumLodLevels + 1;
        expectCapacityFailure(request);
        request = Request();
        request.maximumLayers = capabilities.maximumLayers + 1;
        expectCapacityFailure(request);
        request = Request();
        request.maximumCells = capabilities.maximumCells + 1;
        expectCapacityFailure(request);
        request = Request();
        request.maximumLayerNameBytes = capabilities.maximumLayerNameBytes + 1;
        expectCapacityFailure(request);
        request = Request();
        request.maximumQueryResults = capabilities.maximumQueryResults + 1;
        expectCapacityFailure(request);
    }

    TEST_CASE("Partition settings admit exact grid and capacity boundaries", "[unit][world_streaming][partition_settings]") {
        const auto capabilities = Capabilities();
        auto request = Request();
        request.baseCellSizeMillimeters = capabilities.minimumCellSizeMillimeters;
        request.lodLevels = capabilities.maximumLodLevels;
        request.maximumLayers = capabilities.maximumLayers;
        request.maximumCells = capabilities.maximumCells;
        request.maximumLayerNameBytes = capabilities.maximumLayerNameBytes;
        request.maximumQueryResults = capabilities.maximumQueryResults;
        REQUIRE(WorldPartitionProjectSettings::Create(request, capabilities).HasValue());
        request.baseCellSizeMillimeters = capabilities.maximumCellSizeMillimeters;
        REQUIRE(WorldPartitionProjectSettings::Create(request, capabilities).HasValue());
    }

    TEST_CASE("Partition settings reject unsupported precision and exact package combinations",
              "[unit][world_streaming][partition_settings]") {
        auto request = Request();
        request.precision = WorldPartitionPrecision::Count;
        REQUIRE(ErrorCode(WorldPartitionProjectSettings::Create(request, Capabilities()).ErrorValue()) ==
                WorldStreamingErrors::PartitionSettingsInvalid.code.Value());

        request = Request();
        request.profile = WorldPartitionProjectProfile::Server;
        REQUIRE(ErrorCode(WorldPartitionProjectSettings::Create(request, Capabilities()).ErrorValue()) ==
                WorldStreamingErrors::PartitionSettingsUnsupported.code.Value());

        request.packageMode = WorldPartitionPackageMode::ArchiveChunk;
        auto capabilities = Capabilities();
        capabilities.packages = WorldPartitionPackageCapabilities::StandaloneCellFile;
        REQUIRE(ErrorCode(WorldPartitionProjectSettings::Create(request, capabilities).ErrorValue()) ==
                WorldStreamingErrors::PartitionSettingsUnsupported.code.Value());
    }

    TEST_CASE("Partition settings admission fences replacement cancellation and shutdown", "[unit][world_streaming][partition_settings]") {
        const auto settings = WorldPartitionProjectSettings::Create(Request(), Capabilities()).Value();
        const auto requireStale = [&](const WorldPartitionSettingsId settingsId, const WorldPartitionSettingsRevision settingsRevision,
                                      const WorldPartitionCapabilityId capabilityId,
                                      const WorldPartitionCapabilityRevision capabilityRevision) {
            const auto result = ValidateWorldPartitionSettingsAdmission(settings, settingsId, settingsRevision, capabilityId,
                                                                        capabilityRevision, WorldPartitionSettingsLifecycle::Active);
            REQUIRE(result.HasError());
            REQUIRE(ErrorCode(result.ErrorValue()) == WorldStreamingErrors::PartitionSettingsStale.code.Value());
        };
        REQUIRE(ValidateWorldPartitionSettingsAdmission(settings, Id<WorldPartitionSettingsId>(2), Id<WorldPartitionSettingsRevision>(3),
                                                        Id<WorldPartitionCapabilityId>(5), Id<WorldPartitionCapabilityRevision>(7),
                                                        WorldPartitionSettingsLifecycle::Active)
                    .HasValue());
        requireStale(Id<WorldPartitionSettingsId>(2), Id<WorldPartitionSettingsRevision>(4), Id<WorldPartitionCapabilityId>(5),
                     Id<WorldPartitionCapabilityRevision>(7));
        requireStale(Id<WorldPartitionSettingsId>(9), Id<WorldPartitionSettingsRevision>(3), Id<WorldPartitionCapabilityId>(5),
                     Id<WorldPartitionCapabilityRevision>(7));
        requireStale(Id<WorldPartitionSettingsId>(2), Id<WorldPartitionSettingsRevision>(3), Id<WorldPartitionCapabilityId>(5),
                     Id<WorldPartitionCapabilityRevision>(8));
        requireStale(Id<WorldPartitionSettingsId>(2), Id<WorldPartitionSettingsRevision>(3), Id<WorldPartitionCapabilityId>(9),
                     Id<WorldPartitionCapabilityRevision>(7));
        for (const auto lifecycle : {WorldPartitionSettingsLifecycle::Cancelling, WorldPartitionSettingsLifecycle::Closed}) {
            REQUIRE(
                ErrorCode(ValidateWorldPartitionSettingsAdmission(settings, Id<WorldPartitionSettingsId>(2),
                                                                  Id<WorldPartitionSettingsRevision>(3), Id<WorldPartitionCapabilityId>(5),
                                                                  Id<WorldPartitionCapabilityRevision>(7), lifecycle)
                              .ErrorValue()) == WorldStreamingErrors::PartitionSettingsLifecycleUnavailable.code.Value());
        }
        REQUIRE(ErrorCode(ValidateWorldPartitionSettingsAdmission(settings, Id<WorldPartitionSettingsId>(2),
                                                                  Id<WorldPartitionSettingsRevision>(3), Id<WorldPartitionCapabilityId>(5),
                                                                  Id<WorldPartitionCapabilityRevision>(7),
                                                                  WorldPartitionSettingsLifecycle::Count)
                              .ErrorValue()) == WorldStreamingErrors::PartitionSettingsInvalid.code.Value());
    }
}  // namespace Horo::WorldStreaming
