#include "Horo/Prefab/PrefabDocument.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Prefab {
    namespace {
        Assets::AssetId Asset(const std::uint16_t suffix = 1) {
            std::array<std::uint8_t, 16> bytes{};
            bytes[14] = static_cast<std::uint8_t>(suffix >> 8U);
            bytes[15] = static_cast<std::uint8_t>(suffix);
            return Assets::AssetId::FromBytes(bytes);
        }

        Application::HoroVersion ProjectVersion() {
            return Application::ParseHoroVersion("1.2.3").Value();
        }

        PrefabSourceRevision Revision() {
            Sha256Digest digest{};
            digest.bytes.back() = 7;
            return {.projectVersion = ProjectVersion(), .contentDigest = digest};
        }

        Gameplay::ComponentTypeId ComponentType() {
            return Gameplay::ComponentTypeId::Parse("game.tests.prefab_document").Value();
        }

        Gameplay::BehaviorTypeId BehaviorType() {
            return Gameplay::BehaviorTypeId::Parse("game.tests.prefab_behavior").Value();
        }

        RawComponentPayload Component(const std::uint64_t instance, const std::size_t payloadBytes = 0) {
            return {
                .instance = PrefabComponentInstanceId::Create(instance).Value(),
                .component = {.typeId = ComponentType(), .schemaVersion = 1, .payload = std::vector<std::byte>(payloadBytes)},
            };
        }

        Gameplay::BehaviorComponent Behavior(const std::uint64_t instance) {
            return {
                .instanceId = {instance},
                .typeId = BehaviorType(),
                .schemaVersion = 1,
                .enabled = true,
                .fields = {{.name = "speed", .value = 2.0}},
            };
        }

        PrefabObjectNode Root() {
            return {.localId = {}, .parentLocalId = std::nullopt, .name = "Root"};
        }

        PrefabDocumentData Concrete() {
            return {.projectVersion = ProjectVersion(), .assetId = Asset(1), .objects = {Root()}};
        }

        TEST_CASE("Prefab document publishes one immutable ordered portable candidate", "[unit][prefab][document]") {
            auto data = Concrete();
            data.referencedAssets = {Asset(2)};
            data.objects.front().components = {Component(1)};
            data.objects.front().behaviors = {Behavior(2)};
            data.objects.push_back({.localId = {8}, .parentLocalId = LocalObjectId{}, .name = "Child"});
            data.composition = PrefabComposition{
                .nestedPlacements = {{.placementLocalId = {12},
                                      .parentLocalId = LocalObjectId{8},
                                      .sourcePrefab = PrefabAssetReference::Create(Asset(2)).Value(),
                                      .authoredAgainst = Revision()}},
            };

            const auto document = PrefabDocument::Create(data);
            REQUIRE(document.HasValue());
            REQUIRE(document.Value().Data() == data);
            REQUIRE(document.Value().Data().objects.front().components.front().component.payload.empty());
        }

        TEST_CASE("Prefab hierarchy requires one root and parent-before-child stable slots", "[unit][prefab][document]") {
            auto rootMissing = Concrete();
            rootMissing.objects.front().localId = {1};
            REQUIRE(PrefabDocument::Create(rootMissing).HasError());

            auto duplicate = Concrete();
            duplicate.objects.push_back({.localId = {1}, .parentLocalId = LocalObjectId{}});
            duplicate.objects.push_back({.localId = {1}, .parentLocalId = LocalObjectId{}});
            REQUIRE(PrefabDocument::Create(duplicate).HasError());

            auto unordered = Concrete();
            unordered.objects.push_back({.localId = {2}, .parentLocalId = LocalObjectId{3}});
            unordered.objects.push_back({.localId = {3}, .parentLocalId = LocalObjectId{}});
            REQUIRE(PrefabDocument::Create(unordered).HasError());

            auto secondRoot = Concrete();
            secondRoot.objects.push_back(Root());
            REQUIRE(PrefabDocument::Create(secondRoot).HasError());
        }

        TEST_CASE("Prefab hierarchy accepts exact object and depth bounds then rejects overflow", "[unit][prefab][document]") {
            auto objectBound = Concrete();
            for (std::uint32_t id = 1; id < MaximumPrefabObjectCount; ++id)
                objectBound.objects.push_back({.localId = {id}, .parentLocalId = LocalObjectId{}});
            REQUIRE(PrefabDocument::Create(objectBound).HasValue());
            objectBound.objects.push_back(
                {.localId = {static_cast<std::uint32_t>(MaximumPrefabObjectCount)}, .parentLocalId = LocalObjectId{}});
            REQUIRE(PrefabDocument::Create(objectBound).HasError());

            auto depthBound = Concrete();
            for (std::uint32_t id = 1; id < MaximumPrefabHierarchyDepth; ++id)
                depthBound.objects.push_back({.localId = {id}, .parentLocalId = LocalObjectId{id - 1}});
            REQUIRE(PrefabDocument::Create(depthBound).HasValue());
            depthBound.objects.push_back({.localId = {static_cast<std::uint32_t>(MaximumPrefabHierarchyDepth)},
                                          .parentLocalId = LocalObjectId{static_cast<std::uint32_t>(MaximumPrefabHierarchyDepth - 1)}});
            REQUIRE(PrefabDocument::Create(depthBound).HasError());
        }

        TEST_CASE("Prefab object combines component and behavior occurrences under one bound", "[unit][prefab][document]") {
            auto data = Concrete();
            for (std::uint64_t id = 1; id <= MaximumPrefabComponentsPerObject; ++id)
                data.objects.front().components.push_back(Component(id));
            REQUIRE(PrefabDocument::Create(data).HasValue());
            data.objects.front().behaviors.push_back(Behavior(1));
            REQUIRE(PrefabDocument::Create(data).HasError());

            auto duplicateComponent = Concrete();
            duplicateComponent.objects.front().components = {Component(1), Component(1)};
            REQUIRE(PrefabDocument::Create(duplicateComponent).HasError());

            auto duplicateBehavior = Concrete();
            duplicateBehavior.objects.front().behaviors = {Behavior(1), Behavior(1)};
            REQUIRE(PrefabDocument::Create(duplicateBehavior).HasError());
        }

        TEST_CASE("Prefab dynamic payload accepts exactly four MiB and rejects one byte more", "[unit][prefab][document]") {
            auto data = Concrete();
            data.objects.front().name.clear();
            const std::size_t typeBytes = ComponentType().Value().size();
            const std::size_t baseChunk = (MaximumPrefabPayloadBytes - 4 * typeBytes) / 4;
            const std::size_t remainder = (MaximumPrefabPayloadBytes - 4 * typeBytes) % 4;
            for (std::uint64_t id = 1; id <= 4; ++id)
                data.objects.front().components.push_back(Component(id, baseChunk + (id == 4 ? remainder : 0)));
            REQUIRE(PrefabDocument::Create(data).HasValue());
            data.objects.front().components.back().component.payload.push_back(std::byte{});
            REQUIRE(PrefabDocument::Create(data).HasError());
        }

        TEST_CASE("Prefab composition enforces placement identity source revision and declared dependencies", "[unit][prefab][document]") {
            auto data = Concrete();
            data.referencedAssets = {Asset(2)};
            data.composition = PrefabComposition{
                .nestedPlacements = {{.placementLocalId = {1},
                                      .sourcePrefab = PrefabAssetReference::Create(Asset(2)).Value(),
                                      .authoredAgainst = Revision()}},
            };
            REQUIRE(PrefabDocument::Create(data).HasValue());

            auto colliding = data;
            colliding.composition->nestedPlacements.front().placementLocalId = {};
            REQUIRE(PrefabDocument::Create(colliding).HasError());
            auto undeclared = data;
            undeclared.referencedAssets.clear();
            REQUIRE(PrefabDocument::Create(undeclared).HasError());
            auto selfReference = data;
            selfReference.composition->nestedPlacements.front().sourcePrefab = PrefabAssetReference::Create(Asset(1)).Value();
            selfReference.referencedAssets = {Asset(1)};
            REQUIRE(PrefabDocument::Create(selfReference).HasError());
        }

        TEST_CASE("Prefab direct placement and asset reference counts are independently bounded", "[unit][prefab][document]") {
            auto placements = Concrete();
            placements.referencedAssets = {Asset(2)};
            placements.composition = PrefabComposition{};
            for (std::uint32_t id = 1; id <= MaximumDirectNestedPrefabPlacements; ++id)
                placements.composition->nestedPlacements.push_back({.placementLocalId = {id},
                                                                    .sourcePrefab = PrefabAssetReference::Create(Asset(2)).Value(),
                                                                    .authoredAgainst = Revision()});
            REQUIRE(PrefabDocument::Create(placements).HasValue());
            placements.composition->nestedPlacements.push_back(
                {.placementLocalId = {999}, .sourcePrefab = PrefabAssetReference::Create(Asset(2)).Value(), .authoredAgainst = Revision()});
            REQUIRE(PrefabDocument::Create(placements).HasError());

            auto references = Concrete();
            for (std::uint16_t id = 2; id < MaximumPrefabReferencedAssets + 2; ++id)
                references.referencedAssets.push_back(Asset(id));
            REQUIRE(PrefabDocument::Create(references).HasValue());
            references.referencedAssets.push_back(Asset(500));
            REQUIRE(PrefabDocument::Create(references).HasError());
        }

        TEST_CASE("Prefab variants own one exclusive exact parent source", "[unit][prefab][document]") {
            PrefabDocumentData variant{
                .projectVersion = ProjectVersion(),
                .assetId = Asset(1),
                .composition = PrefabComposition{.variantParent = PrefabAssetReference::Create(Asset(2)).Value(),
                                                 .variantAuthoredAgainst = Revision()},
                .referencedAssets = {Asset(2)},
            };
            REQUIRE(PrefabDocument::Create(variant).HasValue());

            auto withHierarchy = variant;
            withHierarchy.objects = {Root()};
            REQUIRE(PrefabDocument::Create(withHierarchy).HasError());
            auto missingRevision = variant;
            missingRevision.composition->variantAuthoredAgainst.reset();
            REQUIRE(PrefabDocument::Create(missingRevision).HasError());
            auto emptyComposition = Concrete();
            emptyComposition.composition = PrefabComposition{};
            REQUIRE(PrefabDocument::Create(emptyComposition).HasError());
        }

        TEST_CASE("Prefab document validation rejects noncanonical metadata and leaves source candidates untouched",
                  "[unit][prefab][document]") {
            auto invalidVersion = Concrete();
            invalidVersion.projectVersion.prerelease = std::string(80, 'x');
            const auto original = invalidVersion;
            REQUIRE(PrefabDocument::Create(invalidVersion).HasError());
            REQUIRE(invalidVersion == original);

            auto invalidAsset = Concrete();
            invalidAsset.assetId = {};
            REQUIRE(PrefabDocument::Create(invalidAsset).HasError());

            auto invalidTransform = Concrete();
            invalidTransform.objects.front().localTransform.translation.x = std::numeric_limits<float>::quiet_NaN();
            REQUIRE(PrefabDocument::Create(invalidTransform).HasError());

            auto oversizedName = Concrete();
            oversizedName.objects.front().name.assign(MaximumPrefabObjectNameBytes + 1, 'x');
            REQUIRE(PrefabDocument::Create(oversizedName).HasError());

            auto invalidUtf8 = Concrete();
            invalidUtf8.objects.front().name = std::string{"\xC3\x28", 2};
            REQUIRE(PrefabDocument::Create(invalidUtf8).HasError());

            auto validUtf8 = Concrete();
            validUtf8.objects.front().name = std::string{"\xF0\x9F\x8C\x8D", 4};
            REQUIRE(PrefabDocument::Create(validUtf8).HasValue());
        }
    }  // namespace
}  // namespace Horo::Prefab
