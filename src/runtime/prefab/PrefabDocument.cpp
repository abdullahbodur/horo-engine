#include "Horo/Prefab/PrefabDocument.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace Horo::Prefab {
    namespace {
        /** @brief Checks that a public HoroVersion value is a canonical parser-produced project version. */
        [[nodiscard]] bool IsCanonicalProjectVersion(const Application::HoroVersion &version) {
            const auto parsed = Application::ParseHoroVersion(Application::FormatHoroVersion(version));
            return parsed.HasValue() && parsed.Value() == version;
        }

        /** @brief Adds bytes without exceeding the document payload limit. */
        [[nodiscard]] bool AddPayloadBytes(std::size_t &total, const std::size_t bytes) noexcept {
            if (bytes > MaximumPrefabPayloadBytes - total)
                return false;
            total += bytes;
            return true;
        }

        /** @brief Counts dynamic bytes held by a portable behavior field value. */
        [[nodiscard]] std::size_t DynamicValueBytes(const Gameplay::BehaviorFieldValue &value) noexcept {
            return std::visit([]<typename Value>(const Value &typed) -> std::size_t {
                if constexpr (std::is_same_v<Value, std::string>)
                    return typed.size();
                return 0;
            }, value);
        }

        /** @brief Validates one exact source revision without introducing a parallel schema counter. */
        [[nodiscard]] bool IsValidRevision(const PrefabSourceRevision &revision) {
            return IsCanonicalProjectVersion(revision.projectVersion);
        }

        /** @brief Checks whether an AssetId occurs in a validated unique dependency list. */
        [[nodiscard]] bool ContainsAsset(const std::vector<Assets::AssetId> &assets, const Assets::AssetId &asset) {
            return std::ranges::find(assets, asset) != assets.end();
        }

        /** @brief Validates unique component occurrences and accounts for their dynamic bytes. */
        [[nodiscard]] Result<void> ValidateComponents(const std::vector<RawComponentPayload> &components, std::size_t &payloadBytes) {
            std::unordered_set<std::uint64_t> componentInstances;
            componentInstances.reserve(components.size());
            for (const RawComponentPayload &component : components) {
                const auto validation = ValidateRawComponentPayload(component);
                if (validation.HasError())
                    return validation;
                if (!componentInstances.emplace(component.instance.Value()).second)
                    return Result<void>::Failure(MakeError(PrefabErrors::DocumentInvalid));
                if (!AddPayloadBytes(payloadBytes, component.component.typeId.Value().size()) ||
                    !AddPayloadBytes(payloadBytes, component.component.payload.size()))
                    return Result<void>::Failure(MakeError(PrefabErrors::PayloadTooLarge));
            }
            return Result<void>::Success();
        }

        /** @brief Validates unique behavior occurrences and accounts for their dynamic bytes. */
        [[nodiscard]] Result<void> ValidateBehaviors(const std::vector<Gameplay::BehaviorComponent> &behaviors, std::size_t &payloadBytes) {
            std::unordered_set<std::uint64_t> behaviorInstances;
            behaviorInstances.reserve(behaviors.size());
            for (const Gameplay::BehaviorComponent &behavior : behaviors) {
                const auto validation = Gameplay::ValidateBehaviorComponent(behavior);
                if (validation.HasError())
                    return validation;
                if (!behaviorInstances.emplace(behavior.instanceId.value).second)
                    return Result<void>::Failure(MakeError(PrefabErrors::DocumentInvalid));
                if (!AddPayloadBytes(payloadBytes, behavior.typeId.Value().size()))
                    return Result<void>::Failure(MakeError(PrefabErrors::PayloadTooLarge));
                for (const Gameplay::BehaviorField &field : behavior.fields) {
                    if (!AddPayloadBytes(payloadBytes, field.name.size()) || !AddPayloadBytes(payloadBytes, DynamicValueBytes(field.value)))
                        return Result<void>::Failure(MakeError(PrefabErrors::PayloadTooLarge));
                }
            }
            return Result<void>::Success();
        }

        /** @brief Validates and accounts for one hierarchy object's portable data. */
        [[nodiscard]] Result<void> ValidateObject(const PrefabObjectNode &object, std::size_t &payloadBytes) {
            if (object.name.size() > MaximumPrefabObjectNameBytes || !object.localTransform.TryToMatrix().HasValue())
                return Result<void>::Failure(MakeError(PrefabErrors::DocumentInvalid));
            if (object.components.size() > MaximumPrefabComponentsPerObject ||
                object.behaviors.size() > MaximumPrefabComponentsPerObject - object.components.size())
                return Result<void>::Failure(MakeError(PrefabErrors::ComponentCountExceeded));
            if (!AddPayloadBytes(payloadBytes, object.name.size()))
                return Result<void>::Failure(MakeError(PrefabErrors::PayloadTooLarge));

            const auto componentValidation = ValidateComponents(object.components, payloadBytes);
            if (componentValidation.HasError())
                return componentValidation;
            return ValidateBehaviors(object.behaviors, payloadBytes);
        }

        /** @brief Registers one non-root node and computes its root-inclusive depth. */
        [[nodiscard]] Result<void> RegisterChild(const PrefabObjectNode &object, std::unordered_map<std::uint32_t, std::size_t> &depths) {
            if (object.localId.IsRoot() || !object.parentLocalId)
                return Result<void>::Failure(MakeError(PrefabErrors::HierarchyInvalid));
            const auto parent = depths.find(object.parentLocalId->value);
            if (parent == depths.end() || !depths.emplace(object.localId.value, parent->second + 1).second)
                return Result<void>::Failure(MakeError(PrefabErrors::HierarchyInvalid));
            if (parent->second + 1 > MaximumPrefabHierarchyDepth)
                return Result<void>::Failure(MakeError(PrefabErrors::HierarchyDepthExceeded));
            return Result<void>::Success();
        }

        /** @brief Validates root-first parent-before-child hierarchy ordering and depth. */
        [[nodiscard]] Result<void> ValidateHierarchy(const std::vector<PrefabObjectNode> &objects, std::size_t &payloadBytes) {
            if (objects.empty() || !objects.front().localId.IsRoot() || objects.front().parentLocalId)
                return Result<void>::Failure(MakeError(PrefabErrors::HierarchyInvalid));

            std::unordered_map<std::uint32_t, std::size_t> depths;
            depths.reserve(objects.size());
            depths.emplace(0, 1);
            for (std::size_t index = 0; index < objects.size(); ++index) {
                const PrefabObjectNode &object = objects[index];
                if (index != 0) {
                    const auto registration = RegisterChild(object, depths);
                    if (registration.HasError())
                        return registration;
                }

                const auto objectValidation = ValidateObject(object, payloadBytes);
                if (objectValidation.HasError())
                    return objectValidation;
            }
            return Result<void>::Success();
        }

        /** @brief Validates one exclusive variant source. */
        [[nodiscard]] Result<void> ValidateVariant(const PrefabDocumentData &candidate, const PrefabComposition &composition) {
            if (!candidate.objects.empty() || !composition.nestedPlacements.empty() || !composition.variantAuthoredAgainst ||
                composition.variantParent->Asset() == candidate.assetId || !IsValidRevision(*composition.variantAuthoredAgainst) ||
                !ContainsAsset(candidate.referencedAssets, composition.variantParent->Asset()))
                return Result<void>::Failure(MakeError(PrefabErrors::CompositionInvalid));
            return Result<void>::Success();
        }

        /** @brief Validates concrete nested placements and their occupied local slots. */
        [[nodiscard]] bool IsValidNestedPlacement(const PrefabDocumentData &candidate, const std::unordered_set<std::uint32_t> &objectIds,
                                                  const NestedPrefabPlacement &placement) {
            const LocalObjectId parent = placement.parentLocalId.value_or(LocalObjectId{});
            return !placement.placementLocalId.IsRoot() && objectIds.contains(parent.value) && placement.sourcePrefab.IsValid() &&
                   placement.sourcePrefab.Asset() != candidate.assetId && IsValidRevision(placement.authoredAgainst) &&
                   placement.localRootTransform.TryToMatrix().HasValue() &&
                   ContainsAsset(candidate.referencedAssets, placement.sourcePrefab.Asset());
        }

        /** @brief Validates concrete nested placements and their occupied local slots. */
        [[nodiscard]] Result<void> ValidateNestedPlacements(const PrefabDocumentData &candidate,
                                                            const std::unordered_set<std::uint32_t> &objectIds,
                                                            const PrefabComposition &composition) {
            if (composition.nestedPlacements.size() > MaximumDirectNestedPrefabPlacements)
                return Result<void>::Failure(MakeError(PrefabErrors::NestedPlacementCountExceeded));
            std::unordered_set<std::uint32_t> occupiedIds = objectIds;
            occupiedIds.reserve(objectIds.size() + composition.nestedPlacements.size());
            for (const NestedPrefabPlacement &placement : composition.nestedPlacements) {
                if (!occupiedIds.emplace(placement.placementLocalId.value).second ||
                    !IsValidNestedPlacement(candidate, objectIds, placement))
                    return Result<void>::Failure(MakeError(PrefabErrors::CompositionInvalid));
            }
            return Result<void>::Success();
        }

        /** @brief Validates optional concrete nesting or exclusive variant composition. */
        [[nodiscard]] Result<void> ValidateComposition(const PrefabDocumentData &candidate,
                                                       const std::unordered_set<std::uint32_t> &objectIds) {
            if (!candidate.composition)
                return Result<void>::Success();

            const PrefabComposition &composition = *candidate.composition;
            if (composition.variantParent)
                return ValidateVariant(candidate, composition);
            if (composition.variantAuthoredAgainst || composition.nestedPlacements.empty())
                return Result<void>::Failure(MakeError(PrefabErrors::CompositionInvalid));
            return ValidateNestedPlacements(candidate, objectIds, composition);
        }

        /** @brief Validates a unique bounded dependency list and returns its initial payload byte count. */
        [[nodiscard]] Result<std::size_t> ValidateReferences(const std::vector<Assets::AssetId> &references) {
            if (references.size() > MaximumPrefabReferencedAssets)
                return Result<std::size_t>::Failure(MakeError(PrefabErrors::ReferenceCountExceeded));

            std::vector<Assets::AssetId> uniqueAssets;
            uniqueAssets.reserve(references.size());
            std::size_t payloadBytes{};
            for (const Assets::AssetId &asset : references) {
                if (!asset.IsValid() || ContainsAsset(uniqueAssets, asset))
                    return Result<std::size_t>::Failure(MakeError(PrefabErrors::ReferenceInvalid));
                uniqueAssets.push_back(asset);
                if (!AddPayloadBytes(payloadBytes, asset.Bytes().size()))
                    return Result<std::size_t>::Failure(MakeError(PrefabErrors::PayloadTooLarge));
            }
            return Result<std::size_t>::Success(payloadBytes);
        }
    }  // namespace

    /** @copydoc PrefabDocument::Create */
    Result<PrefabDocument> PrefabDocument::Create(PrefabDocumentData candidate) {
        if (!candidate.assetId.IsValid() || !IsCanonicalProjectVersion(candidate.projectVersion))
            return Result<PrefabDocument>::Failure(MakeError(PrefabErrors::DocumentInvalid));
        if (candidate.objects.size() > MaximumPrefabObjectCount)
            return Result<PrefabDocument>::Failure(MakeError(PrefabErrors::ObjectCountExceeded));
        const auto referenceValidation = ValidateReferences(candidate.referencedAssets);
        if (referenceValidation.HasError())
            return Result<PrefabDocument>::Failure(referenceValidation.ErrorValue());
        std::size_t payloadBytes = referenceValidation.Value();

        const bool isVariant = candidate.composition && candidate.composition->variantParent;
        if (!isVariant) {
            const auto hierarchyValidation = ValidateHierarchy(candidate.objects, payloadBytes);
            if (hierarchyValidation.HasError())
                return Result<PrefabDocument>::Failure(hierarchyValidation.ErrorValue());
        }

        std::unordered_set<std::uint32_t> objectIds;
        objectIds.reserve(candidate.objects.size());
        for (const PrefabObjectNode &object : candidate.objects)
            objectIds.emplace(object.localId.value);
        const auto compositionValidation = ValidateComposition(candidate, objectIds);
        if (compositionValidation.HasError())
            return Result<PrefabDocument>::Failure(compositionValidation.ErrorValue());

        return Result<PrefabDocument>::Success(PrefabDocument{std::move(candidate)});
    }

    /** @copydoc PrefabDocument::Data */
    const PrefabDocumentData &PrefabDocument::Data() const noexcept {
        return data_;
    }
}  // namespace Horo::Prefab
