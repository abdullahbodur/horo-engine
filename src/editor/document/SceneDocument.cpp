#include "editor/document/SceneDocument.h"

#include "Horo/Runtime/Scene/PrimitiveMesh.h"
#include "editor/project_model/EditorModelErrors.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

namespace Horo::Editor {
    /** @copydoc IsValidSceneObjectName */
    bool IsValidSceneObjectName(const std::string_view name) noexcept {
        return !name.empty() && name.size() <= MaximumSceneObjectNameBytes;
    }

    /** @copydoc IsValidCameraComponent */
    bool IsValidCameraComponent(const Runtime::CameraComponent &camera) noexcept {
        using enum Runtime::CameraProjection;
        const bool projectionValid = camera.projection == Perspective || camera.projection == Orthographic;
        if (const bool commonValuesValid = std::isfinite(camera.verticalFieldOfViewRadians) && std::isfinite(camera.orthographicHeight) &&
                                           std::isfinite(camera.nearPlane) && std::isfinite(camera.farPlane) && camera.nearPlane > 0.0F &&
                                           camera.farPlane > camera.nearPlane;
            !projectionValid || !commonValuesValid)
            return false;
        if (camera.projection == Perspective) {
            return camera.verticalFieldOfViewRadians > 0.0F && camera.verticalFieldOfViewRadians < Math::Pi;
        }
        return camera.orthographicHeight > 0.0F;
    }

    /** @copydoc IsValidLightComponent */
    bool IsValidLightComponent(const Runtime::LightComponent &light) noexcept {
        using enum Runtime::LightKind;
        const bool kindValid = light.kind == Directional || light.kind == Point || light.kind == Spot;
        return kindValid && Math::IsFinite(light.color) && light.color.x >= 0.0F && light.color.y >= 0.0F && light.color.z >= 0.0F &&
               std::isfinite(light.intensity) && light.intensity >= 0.0F && std::isfinite(light.range) && light.range >= 0.0F &&
               std::isfinite(light.innerConeRadians) && light.innerConeRadians >= 0.0F && std::isfinite(light.outerConeRadians) &&
               light.outerConeRadians >= light.innerConeRadians && light.outerConeRadians <= Math::Pi;
    }

    /** @copydoc IsValidAudioSourceComponent */
    bool IsValidAudioSourceComponent(const Runtime::AudioSourceComponent &audioSource) noexcept {
        return std::isfinite(audioSource.gain) && audioSource.gain >= 0.0F;
    }

    /** @copydoc ResolveSceneObjectEditorState */
    std::optional<ResolvedSceneObjectEditorState> ResolveSceneObjectEditorState(const std::span<const SceneObjectSnapshot> objects,
                                                                                const SceneObjectId object) noexcept {
        const auto local = std::ranges::find(objects, object, &SceneObjectSnapshot::id);
        if (local == objects.end())
            return std::nullopt;

        ResolvedSceneObjectEditorState resolved{.local = local->editorState,
                                                .effectivelyVisible = local->editorState.visible,
                                                .effectivelyLocked = local->editorState.locked};
        std::optional<SceneObjectId> parent = local->parent;
        std::size_t remainingDepth = objects.size();
        while (parent.has_value()) {
            if (remainingDepth-- == 0)
                return std::nullopt;
            const auto ancestor = std::ranges::find(objects, *parent, &SceneObjectSnapshot::id);
            if (ancestor == objects.end())
                return std::nullopt;
            if (!ancestor->editorState.visible) {
                resolved.effectivelyVisible = false;
                resolved.hiddenByParent = true;
            }
            if (ancestor->editorState.locked) {
                resolved.effectivelyLocked = true;
                resolved.lockedByParent = true;
            }
            parent = ancestor->parent;
        }
        return resolved;
    }

    namespace {
        constexpr std::size_t kMaximumHistoryEntries = 256;
        constexpr std::size_t kMaximumHistoryBytes = 4U * 1024U * 1024U;

        struct CreatedObjectDelta {
            SceneObjectSnapshot object;
            std::size_t index{0};
            DocumentChangeKind kind{DocumentChangeKind::Created};
        };

        struct RenamedObjectDelta {
            SceneObjectId object;
            std::string before;
            std::string after;
        };

        struct TransformedObjectDelta {
            SceneObjectId object;
            Math::Transform before;
            Math::Transform after;
        };

        struct TransformedObjectsDelta {
            std::vector<TransformedObjectDelta> objects;
        };

        struct CameraChangedDelta {
            SceneObjectId object;
            Runtime::CameraComponent before;
            Runtime::CameraComponent after;
        };

        struct LightChangedDelta {
            SceneObjectId object;
            Runtime::LightComponent before;
            Runtime::LightComponent after;
        };

        struct TriggerVolumeChangedDelta {
            SceneObjectId object;
            Runtime::TriggerVolumeComponent before;
            Runtime::TriggerVolumeComponent after;
        };

        struct AudioSourceChangedDelta {
            SceneObjectId object;
            Runtime::AudioSourceComponent before;
            Runtime::AudioSourceComponent after;
        };

        struct EditorStateChangedDelta {
            SceneObjectId object;
            SceneObjectEditorState before;
            SceneObjectEditorState after;
        };

        struct ComponentAddedDelta {
            SceneObjectId object;
            ComponentType type;
        };

        struct ComponentRemovedDelta {
            SceneObjectId object;
            ComponentType type;
            std::optional<Runtime::CameraComponent> camera;
            std::optional<Runtime::LightComponent> light;
            std::optional<Runtime::TriggerVolumeComponent> triggerVolume;
            std::optional<Runtime::AudioSourceComponent> audioSource;
        };

        struct BehaviorsChangedDelta {
            SceneObjectId object;
            std::vector<Gameplay::BehaviorComponent> before;
            std::vector<Gameplay::BehaviorComponent> after;
        };

        struct IndexedSceneObject {
            SceneObjectSnapshot object;
            std::size_t index{0};
        };

        struct DeletedObjectsDelta {
            std::vector<SceneObjectId> roots;
            std::vector<IndexedSceneObject> objects;
        };

        using SceneCommandDelta =
            std::variant<CreatedObjectDelta, RenamedObjectDelta, TransformedObjectDelta, TransformedObjectsDelta, CameraChangedDelta,
                         LightChangedDelta, TriggerVolumeChangedDelta, AudioSourceChangedDelta, EditorStateChangedDelta,
                         ComponentAddedDelta, ComponentRemovedDelta, BehaviorsChangedDelta, DeletedObjectsDelta>;

        struct HistoryRecord {
            DocumentStateId beforeState;
            DocumentStateId afterState;
            SceneCommandDelta delta;
            std::vector<SceneObjectId> affectedObjects;
            std::size_t memoryBytes{0};
        };

        [[nodiscard]] Error MakeDocumentError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] bool IsValid(const Math::Transform &transform) noexcept {
            const Math::Quaternion rotation = transform.rotation;
            const float rotationLengthSquared =
                rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
            return Math::IsFinite(transform.translation) && Math::IsFinite(transform.scale) && std::isfinite(rotation.x) &&
                   std::isfinite(rotation.y) && std::isfinite(rotation.z) && std::isfinite(rotation.w) && rotationLengthSquared > 0.0F;
        }

        [[nodiscard]] auto FindObject(std::vector<SceneObjectSnapshot> &objects, const SceneObjectId id) {
            return std::ranges::find(objects, id, &SceneObjectSnapshot::id);
        }

        [[nodiscard]] auto FindObject(const std::vector<SceneObjectSnapshot> &objects, const SceneObjectId id) {
            return std::ranges::find(objects, id, &SceneObjectSnapshot::id);
        }

        [[nodiscard]] bool IsEffectivelyLocked(const std::vector<SceneObjectSnapshot> &objects, const SceneObjectId object) noexcept {
            const std::optional<ResolvedSceneObjectEditorState> state = ResolveSceneObjectEditorState(objects, object);
            return state.has_value() && state->effectivelyLocked;
        }

        [[nodiscard]] Error LockedObjectError() {
            return MakeDocumentError(SceneDocumentErrors::ObjectLocked, "Scene object or one of its ancestors is locked in the editor.");
        }

        [[nodiscard]] Result<void> ValidateDescriptor(const std::optional<PrimitiveMeshDescriptor> &descriptor) {
            if (!descriptor.has_value()) {
                return Result<void>::Success();
            }
            if (descriptor->version.value != 1 || Runtime::PrimitiveCatalog::Find(descriptor->type) == nullptr ||
                Runtime::PrimitiveMeshGenerator::Generate(*descriptor).HasError()) {
                return Result<void>::Failure(
                    MakeDocumentError(SceneDocumentErrors::InvalidPrimitive, "Primitive mesh descriptor is not supported."));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateComponents(const SceneObjectComponentSet &components) {
            if (components.camera.has_value()) {
                const Runtime::CameraComponent &camera = *components.camera;
                if (!IsValidCameraComponent(camera)) {
                    return Result<void>::Failure(
                        MakeDocumentError(SceneDocumentErrors::InvalidCamera, "Camera authoring values are invalid."));
                }
            }
            if (components.light.has_value()) {
                const Runtime::LightComponent &light = *components.light;
                if (!IsValidLightComponent(light)) {
                    return Result<void>::Failure(
                        MakeDocumentError(SceneDocumentErrors::InvalidLight, "Light authoring values are invalid."));
                }
            }
            if (components.audioSource.has_value() && !std::isfinite(components.audioSource->gain)) {
                return Result<void>::Failure(
                    MakeDocumentError(SceneDocumentErrors::InvalidAudioSource, "Audio source gain must be finite."));
            }
            std::vector<Gameplay::BehaviorInstanceId> behaviorIds;
            behaviorIds.reserve(components.behaviors.size());
            for (const Gameplay::BehaviorComponent &behavior : components.behaviors) {
                if (Gameplay::ValidateBehaviorComponent(behavior).HasError() ||
                    std::ranges::find(behaviorIds, behavior.instanceId) != behaviorIds.end()) {
                    return Result<void>::Failure(
                        MakeDocumentError(SceneDocumentErrors::InvalidBehavior, "Behavior attachment payload is invalid."));
                }
                behaviorIds.push_back(behavior.instanceId);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] std::size_t EstimateBehaviorMemoryBytes(const std::vector<Gameplay::BehaviorComponent> &behaviors) noexcept {
            std::size_t total = behaviors.size() * sizeof(Gameplay::BehaviorComponent);
            for (const Gameplay::BehaviorComponent &behavior : behaviors) {
                total += behavior.typeId.Value().size();
                for (const Gameplay::BehaviorField &field : behavior.fields) {
                    total += field.name.size();
                    if (const auto *text = std::get_if<std::string>(&field.value))
                        total += text->size();
                }
            }
            return total;
        }

        template <typename Delta> [[nodiscard]] std::size_t EstimateTypedDeltaMemoryBytes(const Delta &) noexcept {
            return sizeof(Delta);
        }

        [[nodiscard]] std::size_t EstimateTypedDeltaMemoryBytes(const CreatedObjectDelta &delta) noexcept {
            return sizeof(delta) + delta.object.name.size();
        }

        [[nodiscard]] std::size_t EstimateTypedDeltaMemoryBytes(const RenamedObjectDelta &delta) noexcept {
            return sizeof(delta) + delta.before.size() + delta.after.size();
        }

        [[nodiscard]] std::size_t EstimateTypedDeltaMemoryBytes(const DeletedObjectsDelta &delta) noexcept {
            std::size_t bytes = sizeof(delta) + delta.objects.size() * sizeof(IndexedSceneObject);
            for (const IndexedSceneObject &object : delta.objects)
                bytes += object.object.name.size();
            return bytes;
        }

        [[nodiscard]] std::size_t EstimateTypedDeltaMemoryBytes(const TransformedObjectsDelta &delta) noexcept {
            return sizeof(delta) + delta.objects.size() * sizeof(TransformedObjectDelta);
        }

        [[nodiscard]] std::size_t EstimateTypedDeltaMemoryBytes(const BehaviorsChangedDelta &delta) noexcept {
            return sizeof(delta) + EstimateBehaviorMemoryBytes(delta.before) + EstimateBehaviorMemoryBytes(delta.after);
        }

        [[nodiscard]] std::size_t EstimateMemoryBytes(const SceneCommandDelta &delta, const std::size_t affectedObjectCount) noexcept {
            return affectedObjectCount * sizeof(SceneObjectId) + std::visit([]<typename Delta>(const Delta &typedDelta) {
                return EstimateTypedDeltaMemoryBytes(typedDelta);
            }, delta);
        }

        [[nodiscard]] SceneObjectId DeltaRootObject(const SceneCommandDelta &delta) noexcept {
            return std::visit([]<typename Delta>(const Delta &typedDelta) {
                if constexpr (std::is_same_v<Delta, CreatedObjectDelta>) {
                    return typedDelta.object.id;
                } else if constexpr (std::is_same_v<Delta, DeletedObjectsDelta>) {
                    return typedDelta.roots.empty() ? SceneObjectId{} : typedDelta.roots.front();
                } else if constexpr (std::is_same_v<Delta, TransformedObjectsDelta>) {
                    return typedDelta.objects.empty() ? SceneObjectId{} : typedDelta.objects.front().object;
                } else {
                    return typedDelta.object;
                }
            }, delta);
        }

        void AddComponent(SceneObjectComponentSet &components, const ComponentType type) {
            using enum ComponentType;
            switch (type) {
                case Camera:
                    components.camera = Runtime::CameraComponent{};
                    break;
                case Light:
                    components.light = Runtime::LightComponent{};
                    break;
                case TriggerVolume:
                    components.triggerVolume = Runtime::TriggerVolumeComponent{};
                    break;
                case AudioSource:
                    components.audioSource = Runtime::AudioSourceComponent{};
                    break;
                default:
                    break;
            }
        }

        [[nodiscard]] bool HasComponent(const SceneObjectComponentSet &components, const ComponentType type) noexcept {
            using enum ComponentType;
            switch (type) {
                case Camera:
                    return components.camera.has_value();
                case Light:
                    return components.light.has_value();
                case TriggerVolume:
                    return components.triggerVolume.has_value();
                case AudioSource:
                    return components.audioSource.has_value();
                default:
                    return false;
            }
        }

        void RemoveComponent(SceneObjectComponentSet &components, const ComponentType type) {
            using enum ComponentType;
            switch (type) {
                case Camera:
                    components.camera = std::nullopt;
                    break;
                case Light:
                    components.light = std::nullopt;
                    break;
                case TriggerVolume:
                    components.triggerVolume = std::nullopt;
                    break;
                case AudioSource:
                    components.audioSource = std::nullopt;
                    break;
                default:
                    break;
            }
        }

        void RestoreComponent(SceneObjectComponentSet &components, const ComponentRemovedDelta &delta) {
            using enum ComponentType;
            switch (delta.type) {
                case Camera:
                    components.camera = delta.camera;
                    break;
                case Light:
                    components.light = delta.light;
                    break;
                case TriggerVolume:
                    components.triggerVolume = delta.triggerVolume;
                    break;
                case AudioSource:
                    components.audioSource = delta.audioSource;
                    break;
                default:
                    break;
            }
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const CreatedObjectDelta &delta) {
            const std::size_t index = std::min(delta.index, objects.size());
            objects.insert(objects.begin() + static_cast<std::ptrdiff_t>(index), delta.object);
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const RenamedObjectDelta &delta) {
            FindObject(objects, delta.object)->name = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const TransformedObjectDelta &delta) {
            FindObject(objects, delta.object)->localTransform = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const TransformedObjectsDelta &delta) {
            for (const TransformedObjectDelta &object : delta.objects)
                FindObject(objects, object.object)->localTransform = object.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const CameraChangedDelta &delta) {
            FindObject(objects, delta.object)->components.camera = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const LightChangedDelta &delta) {
            FindObject(objects, delta.object)->components.light = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const TriggerVolumeChangedDelta &delta) {
            FindObject(objects, delta.object)->components.triggerVolume = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const AudioSourceChangedDelta &delta) {
            if (const auto object = FindObject(objects, delta.object); object != objects.end())
                object->components.audioSource = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const EditorStateChangedDelta &delta) {
            FindObject(objects, delta.object)->editorState = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const BehaviorsChangedDelta &delta) {
            FindObject(objects, delta.object)->components.behaviors = delta.after;
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const ComponentAddedDelta &delta) {
            if (const auto object = FindObject(objects, delta.object); object != objects.end())
                AddComponent(object->components, delta.type);
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const ComponentRemovedDelta &delta) {
            if (const auto object = FindObject(objects, delta.object); object != objects.end())
                RemoveComponent(object->components, delta.type);
        }

        void ApplyTypedDelta(std::vector<SceneObjectSnapshot> &objects, const DeletedObjectsDelta &delta) {
            std::erase_if(objects, [&delta](const SceneObjectSnapshot &object) {
                return std::ranges::any_of(delta.objects, [&object](const IndexedSceneObject &removed) {
                    return removed.object.id == object.id;
                });
            });
        }

        void ApplyDelta(std::vector<SceneObjectSnapshot> &objects, const SceneCommandDelta &delta) {
            std::visit([&objects]<typename Delta>(const Delta &typedDelta) {
                ApplyTypedDelta(objects, typedDelta);
            }, delta);
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const CreatedObjectDelta &delta) {
            std::erase_if(objects, [&delta](const SceneObjectSnapshot &object) {
                return object.id == delta.object.id;
            });
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const RenamedObjectDelta &delta) {
            FindObject(objects, delta.object)->name = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const TransformedObjectDelta &delta) {
            FindObject(objects, delta.object)->localTransform = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const TransformedObjectsDelta &delta) {
            for (const TransformedObjectDelta &object : delta.objects)
                FindObject(objects, object.object)->localTransform = object.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const CameraChangedDelta &delta) {
            FindObject(objects, delta.object)->components.camera = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const LightChangedDelta &delta) {
            FindObject(objects, delta.object)->components.light = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const TriggerVolumeChangedDelta &delta) {
            FindObject(objects, delta.object)->components.triggerVolume = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const AudioSourceChangedDelta &delta) {
            if (const auto object = FindObject(objects, delta.object); object != objects.end())
                object->components.audioSource = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const EditorStateChangedDelta &delta) {
            FindObject(objects, delta.object)->editorState = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const BehaviorsChangedDelta &delta) {
            FindObject(objects, delta.object)->components.behaviors = delta.before;
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const ComponentAddedDelta &delta) {
            if (const auto object = FindObject(objects, delta.object); object != objects.end())
                RemoveComponent(object->components, delta.type);
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const ComponentRemovedDelta &delta) {
            if (const auto object = FindObject(objects, delta.object); object != objects.end())
                RestoreComponent(object->components, delta);
        }

        void RevertTypedDelta(std::vector<SceneObjectSnapshot> &objects, const DeletedObjectsDelta &delta) {
            for (const IndexedSceneObject &removed : delta.objects) {
                const std::size_t index = std::min(removed.index, objects.size());
                objects.insert(objects.begin() + static_cast<std::ptrdiff_t>(index), removed.object);
            }
        }

        void RevertDelta(std::vector<SceneObjectSnapshot> &objects, const SceneCommandDelta &delta) {
            std::visit([&objects]<typename Delta>(const Delta &typedDelta) {
                RevertTypedDelta(objects, typedDelta);
            }, delta);
        }

        [[nodiscard]] Result<void> ValidateLoadedBehaviors(const SceneObjectSnapshot &object,
                                                           std::unordered_set<std::uint64_t> &behaviorIds,
                                                           std::uint64_t &maximumBehaviorId) {
            for (const Gameplay::BehaviorComponent &behavior : object.components.behaviors) {
                if (!behaviorIds.insert(behavior.instanceId.value).second) {
                    return Result<void>::Failure(MakeDocumentError(SceneDocumentErrors::InvalidBehavior,
                                                                   "Loaded behavior instance IDs must be unique across the scene."));
                }
                maximumBehaviorId = std::max(maximumBehaviorId, behavior.instanceId.value);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateLoadedObject(const SceneObjectSnapshot &object, std::unordered_set<std::uint64_t> &objectIds,
                                                        std::unordered_set<std::uint64_t> &behaviorIds, std::uint64_t &maximumObjectId,
                                                        std::uint64_t &maximumBehaviorId) {
            if (!object.id.IsValid() || !objectIds.insert(object.id.value).second) {
                return Result<void>::Failure(
                    MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Loaded scene object IDs must be non-zero and unique."));
            }
            if (!IsValidSceneObjectName(object.name)) {
                return Result<void>::Failure(
                    MakeDocumentError(SceneDocumentErrors::InvalidName, "Loaded scene object names must contain 1 to 128 bytes."));
            }
            if (!IsValid(object.localTransform)) {
                return Result<void>::Failure(
                    MakeDocumentError(SceneDocumentErrors::InvalidTransform, "Loaded scene object transforms must be finite."));
            }
            if (const Result<void> primitive = ValidateDescriptor(object.primitiveMesh); primitive.HasError())
                return primitive;
            if ((object.meshAsset.has_value() && !object.meshAsset->IsValid()) ||
                (object.meshAsset.has_value() && object.primitiveMesh.has_value())) {
                return Result<void>::Failure(
                    MakeDocumentError(SceneDocumentErrors::InvalidPrimitiveMetadata,
                                      "Loaded scene object mesh references must be valid and mutually exclusive."));
            }
            if (const Result<void> components = ValidateComponents(object.components); components.HasError())
                return components;
            if (Result<void> behaviors = ValidateLoadedBehaviors(object, behaviorIds, maximumBehaviorId); behaviors.HasError())
                return behaviors;
            maximumObjectId = std::max(maximumObjectId, object.id.value);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateLoadedObjectHierarchy(const std::vector<SceneObjectSnapshot> &objects,
                                                                 const std::unordered_set<std::uint64_t> &objectIds,
                                                                 const SceneObjectSnapshot &object) {
            if (!object.parent.has_value())
                return Result<void>::Success();
            if (*object.parent == object.id || !objectIds.contains(object.parent->value)) {
                return Result<void>::Failure(
                    MakeDocumentError(SceneDocumentErrors::ParentNotFound,
                                      "Loaded scene object parents must reference a different object in the same scene."));
            }
            std::unordered_set<std::uint64_t> ancestors;
            std::optional<SceneObjectId> ancestor = object.parent;
            while (ancestor.has_value()) {
                if (!ancestors.insert(ancestor->value).second) {
                    return Result<void>::Failure(
                        MakeDocumentError(SceneDocumentErrors::ParentNotFound, "Loaded scene object hierarchy must not contain a cycle."));
                }
                const auto parent = FindObject(objects, *ancestor);
                if (parent == objects.end())
                    break;
                ancestor = parent->parent;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateLoadedHierarchy(const std::vector<SceneObjectSnapshot> &objects,
                                                           const std::unordered_set<std::uint64_t> &objectIds) {
            for (const SceneObjectSnapshot &object : objects) {
                if (Result<void> valid = ValidateLoadedObjectHierarchy(objects, objectIds, object); valid.HasError())
                    return valid;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] std::vector<SceneObjectId> SelectExistingObjects(const std::vector<SceneObjectSnapshot> &objects,
                                                                       const std::span<const SceneObjectId> requested) {
            std::vector<SceneObjectId> selected;
            selected.reserve(requested.size());
            for (const SceneObjectId object : requested) {
                if (!object.IsValid() || FindObject(objects, object) == objects.end() ||
                    std::ranges::find(selected, object) != selected.end())
                    continue;
                selected.push_back(object);
            }
            return selected;
        }

        [[nodiscard]] std::unordered_set<std::uint64_t> ObjectIdSet(const std::span<const SceneObjectId> objects) {
            std::unordered_set<std::uint64_t> ids;
            ids.reserve(objects.size());
            for (const SceneObjectId object : objects)
                ids.insert(object.value);
            return ids;
        }

        [[nodiscard]] bool HasSelectedAncestor(const std::vector<SceneObjectSnapshot> &objects,
                                               const std::unordered_set<std::uint64_t> &selectedIds, const SceneObjectId object) {
            auto current = FindObject(objects, object);
            std::optional<SceneObjectId> ancestor = current->parent;
            while (ancestor.has_value()) {
                if (selectedIds.contains(ancestor->value))
                    return true;
                current = FindObject(objects, *ancestor);
                ancestor = current == objects.end() ? std::nullopt : current->parent;
            }
            return false;
        }

        [[nodiscard]] std::vector<SceneObjectId> DeletionRoots(const std::vector<SceneObjectSnapshot> &objects,
                                                               const std::span<const SceneObjectId> selected) {
            const std::unordered_set<std::uint64_t> selectedIds = ObjectIdSet(selected);
            std::vector<SceneObjectId> roots;
            roots.reserve(selected.size());
            for (const SceneObjectId object : selected) {
                if (!HasSelectedAncestor(objects, selectedIds, object))
                    roots.push_back(object);
            }
            return roots;
        }

        [[nodiscard]] std::vector<SceneObjectId> CollectRemovedObjects(const std::vector<SceneObjectSnapshot> &objects,
                                                                       const std::span<const SceneObjectId> roots,
                                                                       std::unordered_set<std::uint64_t> &removedIds) {
            std::vector<SceneObjectId> removed{roots.begin(), roots.end()};
            std::deque<SceneObjectId> pending{roots.begin(), roots.end()};
            removedIds.reserve(objects.size());
            for (const SceneObjectId root : roots)
                removedIds.insert(root.value);
            while (!pending.empty()) {
                const SceneObjectId parent = pending.front();
                pending.pop_front();
                for (const SceneObjectSnapshot &candidate : objects) {
                    if (candidate.parent != parent || !removedIds.insert(candidate.id.value).second)
                        continue;
                    removed.push_back(candidate.id);
                    pending.push_back(candidate.id);
                }
            }
            return removed;
        }

        [[nodiscard]] DeletedObjectsDelta CaptureDeletedObjects(const std::vector<SceneObjectSnapshot> &objects,
                                                                std::vector<SceneObjectId> roots,
                                                                const std::unordered_set<std::uint64_t> &removedIds) {
            DeletedObjectsDelta deleted{.roots = std::move(roots)};
            deleted.objects.reserve(removedIds.size());
            std::size_t index = 0;
            for (const SceneObjectSnapshot &object : objects) {
                if (removedIds.contains(object.id.value))
                    deleted.objects.emplace_back(object, index);
                ++index;
            }
            return deleted;
        }

        [[nodiscard]] std::optional<std::string> UniqueSiblingName(const std::string_view baseName,
                                                                   const std::optional<SceneObjectId> parent,
                                                                   const std::span<const SceneObjectSnapshot> objects) {
            const auto nameAvailable = [parent, objects](const std::string_view candidate) {
                return std::ranges::none_of(objects, [parent, candidate](const SceneObjectSnapshot &object) {
                    return object.parent == parent && object.name == candidate;
                });
            };
            if (nameAvailable(baseName))
                return std::string{baseName};
            for (std::uint64_t suffix = 2; suffix <= std::numeric_limits<std::uint32_t>::max(); ++suffix) {
                const std::string suffixText = std::format(" {}", suffix);
                const std::size_t prefixLength = MaximumSceneObjectNameBytes - suffixText.size();
                std::string candidate = std::format("{}{}", baseName.substr(0, prefixLength), suffixText);
                if (nameAvailable(candidate))
                    return candidate;
            }
            return std::nullopt;
        }
    }  // namespace

    struct EditorHistory::Impl {
        Impl() {
            undo.reserve(kMaximumHistoryEntries);
            redo.reserve(kMaximumHistoryEntries);
        }

        std::vector<HistoryRecord> undo;
        std::vector<HistoryRecord> redo;
        std::size_t memoryBytes{0};
    };

    namespace {
        template <typename History> void ClearRedo(History &history) noexcept {
            for (const HistoryRecord &entry : history.redo) {
                history.memoryBytes -= entry.memoryBytes;
            }
            history.redo.clear();
        }

        template <typename History> void PushHistory(History &history, HistoryRecord entry) {
            ClearRedo(history);
            history.memoryBytes += entry.memoryBytes;
            history.undo.push_back(std::move(entry));
            while (history.undo.size() > kMaximumHistoryEntries || history.memoryBytes > kMaximumHistoryBytes) {
                history.memoryBytes -= history.undo.front().memoryBytes;
                history.undo.erase(history.undo.begin());
            }
        }

        [[nodiscard]] Result<void> ValidateHistoryDelta(const SceneCommandDelta &delta, const std::size_t affectedObjectCount) {
            if (EstimateMemoryBytes(delta, affectedObjectCount) > kMaximumHistoryBytes) {
                return Result<void>::Failure(MakeDocumentError(SceneDocumentErrors::HistoryEntryTooLarge,
                                                               "Scene command exceeds the semantic history memory budget."));
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc EditorHistory::EditorHistory */
    EditorHistory::EditorHistory() : m_impl(std::make_unique<Impl>()) {}

    /** @copydoc EditorHistory::~EditorHistory */
    EditorHistory::~EditorHistory() = default;

    /** @copydoc EditorHistory::CanUndo */
    bool EditorHistory::CanUndo() const noexcept {
        return !m_impl->undo.empty();
    }

    /** @copydoc EditorHistory::CanRedo */
    bool EditorHistory::CanRedo() const noexcept {
        return !m_impl->redo.empty();
    }

    /** @copydoc EditorHistory::Clear */
    void EditorHistory::Clear() noexcept {
        m_impl->undo.clear();
        m_impl->redo.clear();
        m_impl->memoryBytes = 0;
    }

    /** @copydoc SceneDocument::Revision */
    DocumentRevision SceneDocument::Revision() const noexcept {
        return m_revision;
    }

    /** @copydoc SceneDocument::State */
    DocumentStateId SceneDocument::State() const noexcept {
        return m_state;
    }

    /** @copydoc SceneDocument::SavedRevision */
    DocumentRevision SceneDocument::SavedRevision() const noexcept {
        return m_savedRevision;
    }

    /** @copydoc SceneDocument::SavedState */
    DocumentStateId SceneDocument::SavedState() const noexcept {
        return m_savedState;
    }

    /** @copydoc SceneDocument::IsDirty */
    bool SceneDocument::IsDirty() const noexcept {
        return m_state != m_savedState;
    }

    /** @copydoc SceneDocument::Snapshot */
    SceneDocumentSnapshot SceneDocument::Snapshot() const {
        return SceneDocumentSnapshot{m_revision, m_state, m_objects};
    }

    /** @copydoc SceneDocument::Objects */
    std::span<const SceneObjectSnapshot> SceneDocument::Objects() const noexcept {
        return m_objects;
    }

    /** @copydoc SceneDocument::Contains */
    bool SceneDocument::Contains(const SceneObjectId object) const noexcept {
        return FindObject(m_objects, object) != m_objects.end();
    }

    /** @copydoc SceneDocument::MarkSaved */
    Result<void> SceneDocument::MarkSaved(const DocumentRevision revision, const DocumentStateId state) {
        if (revision > m_revision || !state.IsValid() || state.value >= m_nextStateId) {
            return Result<void>::Failure(MakeDocumentError(SceneDocumentErrors::InvalidSavedState,
                                                           "Saved revision and state must belong to this document session."));
        }
        m_savedRevision = revision;
        m_savedState = state;
        return Result<void>::Success();
    }

    /** @copydoc SceneDocument::LoadSaved */
    Result<void> SceneDocument::LoadSaved(std::vector<SceneObjectSnapshot> objects) {
        std::unordered_set<std::uint64_t> objectIds;
        std::unordered_set<std::uint64_t> behaviorIds;
        objectIds.reserve(objects.size());
        std::uint64_t maximumObjectId = 0;
        std::uint64_t maximumBehaviorId = 0;
        for (const SceneObjectSnapshot &object : objects) {
            if (Result<void> valid = ValidateLoadedObject(object, objectIds, behaviorIds, maximumObjectId, maximumBehaviorId);
                valid.HasError())
                return valid;
        }
        if (maximumObjectId == std::numeric_limits<std::uint64_t>::max() ||
            maximumBehaviorId == std::numeric_limits<std::uint64_t>::max()) {
            return Result<void>::Failure(MakeDocumentError(SceneDocumentErrors::ObjectNotFound,
                                                           "Loaded scene object IDs must leave space for future authored objects."));
        }

        if (Result<void> validHierarchy = ValidateLoadedHierarchy(objects, objectIds); validHierarchy.HasError())
            return validHierarchy;

        m_objects = std::move(objects);
        m_revision = {};
        m_savedRevision = {};
        m_state = DocumentStateId{1};
        m_savedState = m_state;
        m_nextStateId = 2;
        m_nextObjectId = maximumObjectId + 1;
        m_nextBehaviorInstanceId = maximumBehaviorId + 1;
        return Result<void>::Success();
    }

    /** @copydoc SceneDocument::LoadRecovered */
    Result<void> SceneDocument::LoadRecovered(std::vector<SceneObjectSnapshot> objects) {
        if (Result<void> loaded = LoadSaved(std::move(objects)); loaded.HasError())
            return loaded;
        m_revision = DocumentRevision{1};
        m_state = DocumentStateId{2};
        m_nextStateId = 3;
        return Result<void>::Success();
    }

    /** @copydoc SceneDocumentCommandExecutor::SceneDocumentCommandExecutor */
    SceneDocumentCommandExecutor::SceneDocumentCommandExecutor(SceneDocument &document, EditorHistory &history) noexcept
        : m_document(document), m_history(history) {}

    /** @copydoc SceneDocumentCommandExecutor::Execute(const CreateSceneObjectCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const CreateSceneObjectCommand &command) {
        if (!IsValidSceneObjectName(command.name)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidName, "Scene object name must contain 1 to 128 bytes."));
        }
        if (!IsValid(command.localTransform)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidTransform, "Scene object transform must be finite."));
        }
        if (const Result<void> descriptorResult = ValidateDescriptor(command.primitiveMesh); descriptorResult.HasError()) {
            return Result<SceneCommandResult>::Failure(descriptorResult.ErrorValue());
        }
        if ((command.meshAsset.has_value() && !command.meshAsset->IsValid()) ||
            (command.meshAsset.has_value() && command.primitiveMesh.has_value())) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidPrimitiveMetadata,
                                  "Scene object mesh references must be valid and mutually exclusive."));
        }
        if (const Result<void> componentResult = ValidateComponents(command.components); componentResult.HasError()) {
            return Result<SceneCommandResult>::Failure(componentResult.ErrorValue());
        }
        if (command.parent.has_value() && FindObject(m_document.m_objects, *command.parent) == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ParentNotFound, "Scene object parent does not exist."));
        }
        if (command.parent.has_value() && IsEffectivelyLocked(m_document.m_objects, *command.parent))
            return Result<SceneCommandResult>::Failure(LockedObjectError());

        const SceneObjectId id{m_document.m_nextObjectId};
        SceneCommandDelta delta = CreatedObjectDelta{
            .object = SceneObjectSnapshot{.id = id,
                                          .parent = command.parent,
                                          .name = command.name,
                                          .localTransform = command.localTransform,
                                          .primitiveMesh = command.primitiveMesh,
                                          .components = command.components,
                                          .meshAsset = command.meshAsset},
            .index = m_document.m_objects.size(),
            .kind = DocumentChangeKind::Created,
        };
        if (const Result<void> validHistory = ValidateHistoryDelta(delta, 1); validHistory.HasError()) {
            return Result<SceneCommandResult>::Failure(validHistory.ErrorValue());
        }
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);

        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_nextObjectId;
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(
            SceneCommandResult{id, m_document.m_revision, m_document.m_state, DocumentChangeKind::Created, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const RenameSceneObjectCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const RenameSceneObjectCommand &command) {
        if (!IsValidSceneObjectName(command.name)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidName, "Scene object name must contain 1 to 128 bytes."));
        }
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        if (object->name == command.name) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::Renamed, {}, false});
        }

        SceneCommandDelta delta = RenamedObjectDelta{object->id, object->name, command.name};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        if (const Result<void> validHistory = ValidateHistoryDelta(delta, 1); validHistory.HasError()) {
            return Result<SceneCommandResult>::Failure(validHistory.ErrorValue());
        }
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::Renamed, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectTransformCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectTransformCommand &command) {
        return Execute(SetSceneObjectTransformsCommand{{
            SceneObjectTransformUpdate{.object = command.object, .localTransform = command.localTransform},
        }});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectTransformsCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectTransformsCommand &command) {
        if (command.updates.empty()) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{{}, m_document.m_revision, m_document.m_state, DocumentChangeKind::TransformChanged, {}, false});
        }

        std::vector<TransformedObjectDelta> changed;
        changed.reserve(command.updates.size());
        std::vector<SceneObjectId> seen;
        seen.reserve(command.updates.size());
        for (const SceneObjectTransformUpdate &update : command.updates) {
            if (!update.object.IsValid() || !IsValid(update.localTransform)) {
                return Result<SceneCommandResult>::Failure(
                    MakeDocumentError(SceneDocumentErrors::InvalidTransform, "Batch transform values must be finite."));
            }
            if (std::ranges::find(seen, update.object) != seen.end()) {
                return Result<SceneCommandResult>::Failure(
                    MakeDocumentError(SceneDocumentErrors::InvalidTransform, "Batch transform object identities must be unique."));
            }
            seen.push_back(update.object);
            const auto object = FindObject(m_document.m_objects, update.object);
            if (object == m_document.m_objects.end()) {
                return Result<SceneCommandResult>::Failure(
                    MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Batch transform object does not exist."));
            }
            if (IsEffectivelyLocked(m_document.m_objects, update.object))
                return Result<SceneCommandResult>::Failure(LockedObjectError());
            if (object->localTransform != update.localTransform) {
                changed.emplace_back(object->id, object->localTransform, update.localTransform);
            }
        }
        if (changed.empty()) {
            return Result<SceneCommandResult>::Success(SceneCommandResult{command.updates.front().object,
                                                                          m_document.m_revision,
                                                                          m_document.m_state,
                                                                          DocumentChangeKind::TransformChanged,
                                                                          {},
                                                                          false});
        }

        SceneCommandDelta delta = TransformedObjectsDelta{std::move(changed)};
        const std::vector<TransformedObjectDelta> &deltaObjects = std::get<TransformedObjectsDelta>(delta).objects;
        std::vector<SceneObjectId> affected;
        affected.reserve(deltaObjects.size());
        for (const TransformedObjectDelta &object : deltaObjects) {
            affected.push_back(object.object);
        }
        if (const Result<void> validHistory = ValidateHistoryDelta(delta, affected.size()); validHistory.HasError()) {
            return Result<SceneCommandResult>::Failure(validHistory.ErrorValue());
        }
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, affected.size());
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        const SceneObjectId primary = affected.front();
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(SceneCommandResult{primary, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::TransformChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectCameraCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectCameraCommand &command) {
        if (!IsValidCameraComponent(command.camera)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidCamera, "Camera authoring values are invalid."));
        }
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        if (!object->components.camera.has_value()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidCamera, "Scene object has no camera component."));
        }
        if (*object->components.camera == command.camera) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, {}, false});
        }

        SceneCommandDelta delta = CameraChangedDelta{object->id, *object->components.camera, command.camera};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectLightCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectLightCommand &command) {
        if (!IsValidLightComponent(command.light)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidLight, "Light authoring values are invalid."));
        }
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        if (!object->components.light.has_value()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidLight, "Scene object has no light component."));
        }
        if (*object->components.light == command.light) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, {}, false});
        }

        SceneCommandDelta delta = LightChangedDelta{object->id, *object->components.light, command.light};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectTriggerVolumeCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectTriggerVolumeCommand &command) {
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        if (!object->components.triggerVolume.has_value()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidTriggerVolume, "Scene object has no trigger volume component."));
        }
        if (*object->components.triggerVolume == command.triggerVolume) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, {}, false});
        }

        SceneCommandDelta delta = TriggerVolumeChangedDelta{object->id, *object->components.triggerVolume, command.triggerVolume};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectAudioSourceCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectAudioSourceCommand &command) {
        if (!IsValidAudioSourceComponent(command.audioSource)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidAudioSource, "Audio source gain must be finite and non-negative."));
        }
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        if (!object->components.audioSource.has_value()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidAudioSource, "Scene object has no audio source component."));
        }
        if (*object->components.audioSource == command.audioSource) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, {}, false});
        }

        SceneCommandDelta delta = AudioSourceChangedDelta{object->id, *object->components.audioSource, command.audioSource};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectEditorStateCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectEditorStateCommand &command) {
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        if (object->editorState == command.editorState)
            return Result<SceneCommandResult>::Success(
                {command.object, m_document.m_revision, m_document.m_state, DocumentChangeKind::EditorStateChanged, {}, false});

        SceneCommandDelta delta = EditorStateChangedDelta{object->id, object->editorState, command.editorState};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(
            {command.object, m_document.m_revision, m_document.m_state, DocumentChangeKind::EditorStateChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const AddSceneObjectComponentCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const AddSceneObjectComponentCommand &command) {
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());

        if (HasComponent(object->components, command.type)) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, {}, false});
        }

        SceneCommandDelta delta = ComponentAddedDelta{object->id, command.type};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});

        return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const RemoveSceneObjectComponentCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const RemoveSceneObjectComponentCommand &command) {
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());

        if (!HasComponent(object->components, command.type)) {
            return Result<SceneCommandResult>::Success(
                SceneCommandResult{object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, {}, false});
        }

        SceneCommandDelta delta = ComponentRemovedDelta{object->id,
                                                        command.type,
                                                        object->components.camera,
                                                        object->components.light,
                                                        object->components.triggerVolume,
                                                        object->components.audioSource};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});

        return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                                                                      DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const AttachSceneObjectBehaviorCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const AttachSceneObjectBehaviorCommand &command) {
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        if (!command.typeId.IsValid() || command.schemaVersion == 0 ||
            (!command.allowMultiple && std::ranges::any_of(object->components.behaviors, [&](const auto &behavior) {
            return behavior.typeId == command.typeId;
        }))) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidBehavior, "Behavior type cannot be attached more than once to this object."));
        }
        Gameplay::BehaviorComponent behavior{Gameplay::BehaviorInstanceId{m_document.m_nextBehaviorInstanceId}, command.typeId,
                                             command.schemaVersion, command.enabled, command.fields};
        if (Gameplay::ValidateBehaviorComponent(behavior).HasError())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidBehavior, "Behavior attachment payload is invalid."));
        auto after = object->components.behaviors;
        after.push_back(std::move(behavior));
        SceneCommandDelta delta = BehaviorsChangedDelta{object->id, object->components.behaviors, std::move(after)};
        if (const Result<void> valid = ValidateHistoryDelta(delta, 1); valid.HasError())
            return Result<SceneCommandResult>::Failure(valid.ErrorValue());
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_nextBehaviorInstanceId;
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(
            {command.object, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectBehaviorCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectBehaviorCommand &command) {
        if (Gameplay::ValidateBehaviorComponent(command.behavior).HasError())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidBehavior, "Behavior replacement payload is invalid."));
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        const auto behavior =
            std::ranges::find(object->components.behaviors, command.behavior.instanceId, &Gameplay::BehaviorComponent::instanceId);
        if (behavior == object->components.behaviors.end() || behavior->typeId != command.behavior.typeId)
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidBehavior, "Behavior attachment does not exist or changed type."));
        if (*behavior == command.behavior)
            return Result<SceneCommandResult>::Success(
                {command.object, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, {}, false});
        auto after = object->components.behaviors;
        *std::ranges::find(after, command.behavior.instanceId, &Gameplay::BehaviorComponent::instanceId) = command.behavior;
        SceneCommandDelta delta = BehaviorsChangedDelta{object->id, object->components.behaviors, std::move(after)};
        if (const Result<void> valid = ValidateHistoryDelta(delta, 1); valid.HasError())
            return Result<SceneCommandResult>::Failure(valid.ErrorValue());
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(
            {command.object, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const RemoveSceneObjectBehaviorCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const RemoveSceneObjectBehaviorCommand &command) {
        const auto object = FindObject(m_document.m_objects, command.object);
        if (object == m_document.m_objects.end())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        if (IsEffectivelyLocked(m_document.m_objects, command.object))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        auto after = object->components.behaviors;
        if (const auto removed = std::erase_if(after,
                                               [behaviorId = command.behavior](const Gameplay::BehaviorComponent &behavior) {
            return behavior.instanceId == behaviorId;
        });
            removed == 0)
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidBehavior, "Behavior attachment does not exist."));
        SceneCommandDelta delta = BehaviorsChangedDelta{object->id, object->components.behaviors, std::move(after)};
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{object->id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(
            {command.object, m_document.m_revision, m_document.m_state, DocumentChangeKind::ComponentChanged, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const DuplicateSceneObjectCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const DuplicateSceneObjectCommand &command) {
        if (!IsValidSceneObjectName(command.name)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidName, "Scene object name must contain 1 to 128 bytes."));
        }
        const auto source = FindObject(m_document.m_objects, command.source);
        if (source == m_document.m_objects.end()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "Scene object does not exist."));
        }
        if (IsEffectivelyLocked(m_document.m_objects, command.source))
            return Result<SceneCommandResult>::Failure(LockedObjectError());

        const SceneObjectId id{m_document.m_nextObjectId};
        SceneObjectComponentSet duplicatedComponents = source->components;
        for (Gameplay::BehaviorComponent &behavior : duplicatedComponents.behaviors)
            behavior.instanceId = Gameplay::BehaviorInstanceId{m_document.m_nextBehaviorInstanceId++};
        SceneCommandDelta delta = CreatedObjectDelta{
            .object = SceneObjectSnapshot{.id = id,
                                          .parent = source->parent,
                                          .name = command.name,
                                          .localTransform = source->localTransform,
                                          .primitiveMesh = source->primitiveMesh,
                                          .components = std::move(duplicatedComponents),
                                          .meshAsset = source->meshAsset,
                                          .editorState = source->editorState},
            .index = m_document.m_objects.size(),
            .kind = DocumentChangeKind::Duplicated,
        };
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_nextObjectId;
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        std::vector affected{id};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
        return Result<SceneCommandResult>::Success(
            SceneCommandResult{id, m_document.m_revision, m_document.m_state, DocumentChangeKind::Duplicated, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const DeleteSceneObjectCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const DeleteSceneObjectCommand &command) {
        return Execute(DeleteSceneObjectsCommand{{command.object}});
    }

    /** @copydoc SceneDocumentCommandExecutor::Execute(const DeleteSceneObjectsCommand&) */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const DeleteSceneObjectsCommand &command) {
        const std::vector<SceneObjectId> selected = SelectExistingObjects(m_document.m_objects, command.objects);
        if (selected.empty()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ObjectNotFound, "No requested scene object exists in the active document."));
        }

        std::vector<SceneObjectId> roots = DeletionRoots(m_document.m_objects, selected);
        std::unordered_set<std::uint64_t> removedIds;
        std::vector<SceneObjectId> removed = CollectRemovedObjects(m_document.m_objects, roots, removedIds);
        if (std::ranges::any_of(removed, [&](const SceneObjectId object) {
            return IsEffectivelyLocked(m_document.m_objects, object);
        }))
            return Result<SceneCommandResult>::Failure(LockedObjectError());
        const SceneObjectId primary = roots.front();
        SceneCommandDelta delta = CaptureDeletedObjects(m_document.m_objects, std::move(roots), removedIds);
        if (const Result<void> validHistory = ValidateHistoryDelta(delta, removed.size()); validHistory.HasError()) {
            return Result<SceneCommandResult>::Failure(validHistory.ErrorValue());
        }
        const std::size_t memoryBytes = EstimateMemoryBytes(delta, removed.size());
        const DocumentStateId beforeState = m_document.m_state;
        ApplyDelta(m_document.m_objects, delta);
        ++m_document.m_revision.value;
        m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
        PushHistory(*m_history.m_impl, HistoryRecord{beforeState, m_document.m_state, std::move(delta), removed, memoryBytes});
        return Result<SceneCommandResult>::Success(
            SceneCommandResult{primary, m_document.m_revision, m_document.m_state, DocumentChangeKind::Deleted, std::move(removed), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Undo */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Undo() {
        if (m_history.m_impl->undo.empty()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::NothingToUndo, "No committed scene command is available to undo."));
        }
        HistoryRecord entry = std::move(m_history.m_impl->undo.back());
        m_history.m_impl->undo.pop_back();
        RevertDelta(m_document.m_objects, entry.delta);
        ++m_document.m_revision.value;
        m_document.m_state = entry.beforeState;
        const SceneObjectId object = DeltaRootObject(entry.delta);
        std::vector affected = entry.affectedObjects;
        m_history.m_impl->redo.push_back(std::move(entry));
        return Result<SceneCommandResult>::Success(
            SceneCommandResult{object, m_document.m_revision, m_document.m_state, DocumentChangeKind::Undone, std::move(affected), true});
    }

    /** @copydoc SceneDocumentCommandExecutor::Redo */
    Result<SceneCommandResult> SceneDocumentCommandExecutor::Redo() {
        if (m_history.m_impl->redo.empty()) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::NothingToRedo, "No reverted scene command is available to redo."));
        }
        HistoryRecord entry = std::move(m_history.m_impl->redo.back());
        m_history.m_impl->redo.pop_back();
        ApplyDelta(m_document.m_objects, entry.delta);
        ++m_document.m_revision.value;
        m_document.m_state = entry.afterState;
        const SceneObjectId object = DeltaRootObject(entry.delta);
        std::vector affected = entry.affectedObjects;
        m_history.m_impl->undo.push_back(std::move(entry));
        return Result<SceneCommandResult>::Success(
            SceneCommandResult{object, m_document.m_revision, m_document.m_state, DocumentChangeKind::Redone, std::move(affected), true});
    }

    /** @copydoc CreateSceneObjectUseCase::CreateSceneObjectUseCase */
    CreateSceneObjectUseCase::CreateSceneObjectUseCase(SceneDocument &document, SceneDocumentCommandExecutor &executor) noexcept
        : m_document(document), m_executor(executor) {}

    /** @copydoc CreateSceneObjectUseCase::Execute */
    Result<SceneCommandResult> CreateSceneObjectUseCase::Execute(const PrimitiveCreationRequest &request) {
        const Runtime::PrimitiveDescriptor *descriptor = Runtime::PrimitiveCatalog::Find(request.primitive.value);
        if (descriptor == nullptr) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::UnknownPrimitive, "Requested primitive is not registered."));
        }
        if (descriptor->creationGroup == Runtime::PrimitiveCreationGroup::NotCreatable ||
            descriptor->category == Runtime::PrimitiveCategory::Collider) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::PrimitiveNotCreatable, "Requested primitive is not a hierarchy creation object."));
        }
        if (request.parent.has_value() && !m_document.Contains(*request.parent)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ParentNotFound, "Scene object parent does not exist."));
        }

        std::optional<std::string> name = UniqueSiblingName(descriptor->defaultObjectName, request.parent, m_document.Objects());
        if (!name.has_value())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidName, "Unable to allocate a unique primitive object name."));

        CreateSceneObjectCommand command{.name = std::move(*name), .parent = request.parent};
        if (descriptor->meshType.has_value()) {
            command.primitiveMesh = PrimitiveMeshDescriptor::Defaults(*descriptor->meshType);
        } else if (descriptor->sceneObjectType.has_value()) {
            switch (*descriptor->sceneObjectType) {
                case Runtime::SceneObjectPrimitiveType::Empty:
                    break;
                case Runtime::SceneObjectPrimitiveType::Camera:
                    command.components.camera = Runtime::CameraComponent{};
                    break;
                case Runtime::SceneObjectPrimitiveType::DirectionalLight:
                    command.components.light = Runtime::LightComponent{.kind = Runtime::LightKind::Directional};
                    break;
                case Runtime::SceneObjectPrimitiveType::PointLight:
                    command.components.light = Runtime::LightComponent{.kind = Runtime::LightKind::Point};
                    break;
                case Runtime::SceneObjectPrimitiveType::SpotLight:
                    command.components.light = Runtime::LightComponent{.kind = Runtime::LightKind::Spot};
                    break;
                case Runtime::SceneObjectPrimitiveType::TriggerVolume:
                    command.components.triggerVolume = Runtime::TriggerVolumeComponent{};
                    break;
                case Runtime::SceneObjectPrimitiveType::AudioSource:
                    command.components.audioSource = Runtime::AudioSourceComponent{};
                    break;
            }
        } else {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidPrimitiveMetadata, "Creatable primitive has no typed authoring descriptor."));
        }
        return m_executor.Execute(command);
    }

    /** @copydoc InstantiateSceneAssetUseCase::InstantiateSceneAssetUseCase */
    InstantiateSceneAssetUseCase::InstantiateSceneAssetUseCase(SceneDocument &document, SceneDocumentCommandExecutor &executor) noexcept
        : document_(document), executor_(executor) {}

    /** @copydoc InstantiateSceneAssetUseCase::Execute */
    Result<SceneCommandResult> InstantiateSceneAssetUseCase::Execute(const AssetInstantiationRequest &request) {
        if (!request.asset.IsValid() || !IsValidSceneObjectName(request.baseName)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidPrimitiveMetadata, "Asset instantiation request is invalid."));
        }
        if (request.parent.has_value() && !document_.Contains(*request.parent)) {
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::ParentNotFound, "Asset drop parent no longer exists."));
        }

        std::optional<std::string> name = UniqueSiblingName(request.baseName, request.parent, document_.Objects());
        if (!name.has_value())
            return Result<SceneCommandResult>::Failure(
                MakeDocumentError(SceneDocumentErrors::InvalidName, "Unable to allocate a unique asset object name."));
        return executor_.Execute(CreateSceneObjectCommand{.name = std::move(*name),
                                                          .parent = request.parent,
                                                          .localTransform = request.localTransform,
                                                          .components = {},
                                                          .meshAsset = request.asset});
    }
}  // namespace Horo::Editor
