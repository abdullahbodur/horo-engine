#include "editor/document/SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Horo::Editor
{
namespace
{
constexpr std::size_t kMaximumObjectNameBytes = 128;
constexpr std::size_t kMaximumHistoryEntries = 256;
constexpr std::size_t kMaximumHistoryBytes = 4U * 1024U * 1024U;

struct CreatedObjectDelta
{
    SceneObjectSnapshot object;
    std::size_t index{0};
    DocumentChangeKind kind{DocumentChangeKind::Created};
};

struct RenamedObjectDelta
{
    SceneObjectId object;
    std::string before;
    std::string after;
};

struct TransformedObjectDelta
{
    SceneObjectId object;
    Math::Transform before;
    Math::Transform after;
};

struct IndexedSceneObject
{
    SceneObjectSnapshot object;
    std::size_t index{0};
};

struct DeletedObjectsDelta
{
    SceneObjectId root;
    std::vector<IndexedSceneObject> objects;
};

using SceneCommandDelta =
    std::variant<CreatedObjectDelta, RenamedObjectDelta, TransformedObjectDelta, DeletedObjectsDelta>;

struct HistoryRecord
{
    DocumentStateId beforeState;
    DocumentStateId afterState;
    SceneCommandDelta delta;
    std::vector<SceneObjectId> affectedObjects;
    std::size_t memoryBytes{0};
};

[[nodiscard]] Error MakeDocumentError(std::string code, std::string message)
{
    return Error{ErrorCode{std::move(code)},
                 ErrorDomainId{"horo.editor.scene_document"},
                 ErrorSeverity::Error,
                 std::move(message),
                 {}};
}

[[nodiscard]] bool IsValid(const Math::Transform &transform) noexcept
{
    const Math::Quaternion rotation = transform.rotation;
    const float rotationLengthSquared =
        rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
    return Math::IsFinite(transform.translation) && Math::IsFinite(transform.scale) && std::isfinite(rotation.x) &&
           std::isfinite(rotation.y) && std::isfinite(rotation.z) && std::isfinite(rotation.w) &&
           rotationLengthSquared > 0.0F;
}

[[nodiscard]] bool IsValidName(const std::string_view name) noexcept
{
    return !name.empty() && name.size() <= kMaximumObjectNameBytes;
}

[[nodiscard]] auto FindObject(std::vector<SceneObjectSnapshot> &objects, const SceneObjectId id)
{
    return std::ranges::find(objects, id, &SceneObjectSnapshot::id);
}

[[nodiscard]] auto FindObject(const std::vector<SceneObjectSnapshot> &objects, const SceneObjectId id)
{
    return std::ranges::find(objects, id, &SceneObjectSnapshot::id);
}

[[nodiscard]] Result<void> ValidateDescriptor(const std::optional<PrimitiveMeshDescriptor> &descriptor)
{
    if (!descriptor.has_value())
    {
        return Result<void>::Success();
    }
    if (descriptor->version.value != 1 || Runtime::PrimitiveCatalog::Find(descriptor->type) == nullptr ||
        Runtime::PrimitiveMeshGenerator::Generate(*descriptor).HasError())
    {
        return Result<void>::Failure(
            MakeDocumentError("scene_document.invalid_primitive", "Primitive mesh descriptor is not supported."));
    }
    return Result<void>::Success();
}

[[nodiscard]] Result<void> ValidateComponents(const SceneObjectComponentSet &components)
{
    if (components.camera.has_value())
    {
        const Runtime::CameraComponent &camera = *components.camera;
        if (!std::isfinite(camera.verticalFieldOfViewRadians) || !std::isfinite(camera.orthographicHeight) ||
            !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) || camera.nearPlane <= 0.0F ||
            camera.farPlane <= camera.nearPlane)
        {
            return Result<void>::Failure(
                MakeDocumentError("scene_document.invalid_camera", "Camera authoring values are invalid."));
        }
    }
    if (components.light.has_value())
    {
        const Runtime::LightComponent &light = *components.light;
        if (!Math::IsFinite(light.color) || !std::isfinite(light.intensity) || !std::isfinite(light.range) ||
            !std::isfinite(light.innerConeRadians) || !std::isfinite(light.outerConeRadians))
        {
            return Result<void>::Failure(
                MakeDocumentError("scene_document.invalid_light", "Light authoring values are invalid."));
        }
    }
    if (components.audioSource.has_value() && !std::isfinite(components.audioSource->gain))
    {
        return Result<void>::Failure(
            MakeDocumentError("scene_document.invalid_audio_source", "Audio source gain must be finite."));
    }
    return Result<void>::Success();
}

[[nodiscard]] std::size_t EstimateMemoryBytes(const SceneCommandDelta &delta,
                                              const std::size_t affectedObjectCount) noexcept
{
    return affectedObjectCount * sizeof(SceneObjectId) +
           std::visit(
               [](const auto &typedDelta) -> std::size_t {
                   using Delta = std::decay_t<decltype(typedDelta)>;
                   if constexpr (std::is_same_v<Delta, CreatedObjectDelta>)
                   {
                       return sizeof(Delta) + typedDelta.object.name.size();
                   }
                   else if constexpr (std::is_same_v<Delta, RenamedObjectDelta>)
                   {
                       return sizeof(Delta) + typedDelta.before.size() + typedDelta.after.size();
                   }
                   else if constexpr (std::is_same_v<Delta, DeletedObjectsDelta>)
                   {
                       std::size_t bytes = sizeof(Delta) + typedDelta.objects.size() * sizeof(IndexedSceneObject);
                       for (const IndexedSceneObject &object : typedDelta.objects)
                       {
                           bytes += object.object.name.size();
                       }
                       return bytes;
                   }
                   else
                   {
                       return sizeof(Delta);
                   }
               },
               delta);
}

[[nodiscard]] SceneObjectId DeltaRootObject(const SceneCommandDelta &delta) noexcept
{
    return std::visit(
        [](const auto &typedDelta) {
            using Delta = std::decay_t<decltype(typedDelta)>;
            if constexpr (std::is_same_v<Delta, CreatedObjectDelta>)
            {
                return typedDelta.object.id;
            }
            else if constexpr (std::is_same_v<Delta, DeletedObjectsDelta>)
            {
                return typedDelta.root;
            }
            else
            {
                return typedDelta.object;
            }
        },
        delta);
}

void ApplyDelta(std::vector<SceneObjectSnapshot> &objects, const SceneCommandDelta &delta)
{
    std::visit(
        [&objects](const auto &typedDelta) {
            using Delta = std::decay_t<decltype(typedDelta)>;
            if constexpr (std::is_same_v<Delta, CreatedObjectDelta>)
            {
                const std::size_t index = std::min(typedDelta.index, objects.size());
                objects.insert(objects.begin() + static_cast<std::ptrdiff_t>(index), typedDelta.object);
            }
            else if constexpr (std::is_same_v<Delta, RenamedObjectDelta>)
            {
                FindObject(objects, typedDelta.object)->name = typedDelta.after;
            }
            else if constexpr (std::is_same_v<Delta, TransformedObjectDelta>)
            {
                FindObject(objects, typedDelta.object)->localTransform = typedDelta.after;
            }
            else
            {
                std::erase_if(objects, [&typedDelta](const SceneObjectSnapshot &object) {
                    return std::ranges::find_if(typedDelta.objects, [&object](const IndexedSceneObject &removed) {
                               return removed.object.id == object.id;
                           }) != typedDelta.objects.end();
                });
            }
        },
        delta);
}

void RevertDelta(std::vector<SceneObjectSnapshot> &objects, const SceneCommandDelta &delta)
{
    std::visit(
        [&objects](const auto &typedDelta) {
            using Delta = std::decay_t<decltype(typedDelta)>;
            if constexpr (std::is_same_v<Delta, CreatedObjectDelta>)
            {
                std::erase_if(objects, [&typedDelta](const SceneObjectSnapshot &object) {
                    return object.id == typedDelta.object.id;
                });
            }
            else if constexpr (std::is_same_v<Delta, RenamedObjectDelta>)
            {
                FindObject(objects, typedDelta.object)->name = typedDelta.before;
            }
            else if constexpr (std::is_same_v<Delta, TransformedObjectDelta>)
            {
                FindObject(objects, typedDelta.object)->localTransform = typedDelta.before;
            }
            else
            {
                for (const IndexedSceneObject &removed : typedDelta.objects)
                {
                    const std::size_t index = std::min(removed.index, objects.size());
                    objects.insert(objects.begin() + static_cast<std::ptrdiff_t>(index), removed.object);
                }
            }
        },
        delta);
}
} // namespace

struct EditorHistory::Impl
{
    Impl()
    {
        undo.reserve(kMaximumHistoryEntries);
        redo.reserve(kMaximumHistoryEntries);
    }

    std::vector<HistoryRecord> undo;
    std::vector<HistoryRecord> redo;
    std::size_t memoryBytes{0};
};

namespace
{
template <typename History> void ClearRedo(History &history) noexcept
{
    for (const HistoryRecord &entry : history.redo)
    {
        history.memoryBytes -= entry.memoryBytes;
    }
    history.redo.clear();
}

template <typename History> void PushHistory(History &history, HistoryRecord entry)
{
    ClearRedo(history);
    history.memoryBytes += entry.memoryBytes;
    history.undo.push_back(std::move(entry));
    while (history.undo.size() > kMaximumHistoryEntries || history.memoryBytes > kMaximumHistoryBytes)
    {
        history.memoryBytes -= history.undo.front().memoryBytes;
        history.undo.erase(history.undo.begin());
    }
}

[[nodiscard]] Result<void> ValidateHistoryDelta(const SceneCommandDelta &delta, const std::size_t affectedObjectCount)
{
    if (EstimateMemoryBytes(delta, affectedObjectCount) > kMaximumHistoryBytes)
    {
        return Result<void>::Failure(MakeDocumentError("scene_document.history_entry_too_large",
                                                       "Scene command exceeds the semantic history memory budget."));
    }
    return Result<void>::Success();
}
} // namespace

/** @copydoc EditorHistory::EditorHistory */
EditorHistory::EditorHistory() : m_impl(std::make_unique<Impl>())
{
}

/** @copydoc EditorHistory::~EditorHistory */
EditorHistory::~EditorHistory() = default;

/** @copydoc EditorHistory::CanUndo */
bool EditorHistory::CanUndo() const noexcept
{
    return !m_impl->undo.empty();
}

/** @copydoc EditorHistory::CanRedo */
bool EditorHistory::CanRedo() const noexcept
{
    return !m_impl->redo.empty();
}

/** @copydoc EditorHistory::Clear */
void EditorHistory::Clear() noexcept
{
    m_impl->undo.clear();
    m_impl->redo.clear();
    m_impl->memoryBytes = 0;
}

/** @copydoc SceneDocument::Revision */
DocumentRevision SceneDocument::Revision() const noexcept
{
    return m_revision;
}

/** @copydoc SceneDocument::State */
DocumentStateId SceneDocument::State() const noexcept
{
    return m_state;
}

/** @copydoc SceneDocument::IsDirty */
bool SceneDocument::IsDirty() const noexcept
{
    return m_state != m_savedState;
}

/** @copydoc SceneDocument::Snapshot */
SceneDocumentSnapshot SceneDocument::Snapshot() const
{
    return SceneDocumentSnapshot{m_revision, m_state, m_objects};
}

/** @copydoc SceneDocument::Objects */
std::span<const SceneObjectSnapshot> SceneDocument::Objects() const noexcept
{
    return m_objects;
}

/** @copydoc SceneDocument::Contains */
bool SceneDocument::Contains(const SceneObjectId object) const noexcept
{
    return FindObject(m_objects, object) != m_objects.end();
}

/** @copydoc SceneDocument::MarkSaved */
Result<void> SceneDocument::MarkSaved(const DocumentRevision revision, const DocumentStateId state)
{
    if (revision > m_revision || !state.IsValid() || state.value >= m_nextStateId)
    {
        return Result<void>::Failure(MakeDocumentError(
            "scene_document.invalid_saved_state", "Saved revision and state must belong to this document session."));
    }
    m_savedRevision = revision;
    m_savedState = state;
    return Result<void>::Success();
}

/** @copydoc SceneDocumentCommandExecutor::SceneDocumentCommandExecutor */
SceneDocumentCommandExecutor::SceneDocumentCommandExecutor(SceneDocument &document, EditorHistory &history) noexcept
    : m_document(document), m_history(history)
{
}

/** @copydoc SceneDocumentCommandExecutor::Execute(const CreateSceneObjectCommand&) */
Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const CreateSceneObjectCommand &command)
{
    if (!IsValidName(command.name))
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.invalid_name", "Scene object name must contain 1 to 128 bytes."));
    }
    if (!IsValid(command.localTransform))
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.invalid_transform", "Scene object transform must be finite."));
    }
    const Result<void> descriptorResult = ValidateDescriptor(command.primitiveMesh);
    if (descriptorResult.HasError())
    {
        return Result<SceneCommandResult>::Failure(descriptorResult.ErrorValue());
    }
    const Result<void> componentResult = ValidateComponents(command.components);
    if (componentResult.HasError())
    {
        return Result<SceneCommandResult>::Failure(componentResult.ErrorValue());
    }
    if (command.parent.has_value() && FindObject(m_document.m_objects, *command.parent) == m_document.m_objects.end())
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.parent_not_found", "Scene object parent does not exist."));
    }

    const SceneObjectId id{m_document.m_nextObjectId};
    SceneCommandDelta delta = CreatedObjectDelta{
        .object = SceneObjectSnapshot{.id = id,
                                      .parent = command.parent,
                                      .name = command.name,
                                      .localTransform = command.localTransform,
                                      .primitiveMesh = command.primitiveMesh,
                                      .components = command.components},
        .index = m_document.m_objects.size(),
        .kind = DocumentChangeKind::Created,
    };
    if (const Result<void> validHistory = ValidateHistoryDelta(delta, 1); validHistory.HasError())
    {
        return Result<SceneCommandResult>::Failure(validHistory.ErrorValue());
    }
    const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);

    const DocumentStateId beforeState = m_document.m_state;
    ApplyDelta(m_document.m_objects, delta);
    ++m_document.m_nextObjectId;
    ++m_document.m_revision.value;
    m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
    std::vector affected{id};
    PushHistory(*m_history.m_impl,
                HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
    return Result<SceneCommandResult>::Success(SceneCommandResult{
        id, m_document.m_revision, m_document.m_state, DocumentChangeKind::Created, std::move(affected), true});
}

/** @copydoc SceneDocumentCommandExecutor::Execute(const RenameSceneObjectCommand&) */
Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const RenameSceneObjectCommand &command)
{
    if (!IsValidName(command.name))
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.invalid_name", "Scene object name must contain 1 to 128 bytes."));
    }
    const auto object = FindObject(m_document.m_objects, command.object);
    if (object == m_document.m_objects.end())
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.object_not_found", "Scene object does not exist."));
    }
    if (object->name == command.name)
    {
        return Result<SceneCommandResult>::Success(SceneCommandResult{
            object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::Renamed, {}, false});
    }

    SceneCommandDelta delta = RenamedObjectDelta{object->id, object->name, command.name};
    const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
    if (const Result<void> validHistory = ValidateHistoryDelta(delta, 1); validHistory.HasError())
    {
        return Result<SceneCommandResult>::Failure(validHistory.ErrorValue());
    }
    const DocumentStateId beforeState = m_document.m_state;
    ApplyDelta(m_document.m_objects, delta);
    ++m_document.m_revision.value;
    m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
    std::vector affected{object->id};
    PushHistory(*m_history.m_impl,
                HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
    return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision,
                                                                  m_document.m_state, DocumentChangeKind::Renamed,
                                                                  std::move(affected), true});
}

/** @copydoc SceneDocumentCommandExecutor::Execute(const SetSceneObjectTransformCommand&) */
Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const SetSceneObjectTransformCommand &command)
{
    if (!IsValid(command.localTransform))
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.invalid_transform", "Scene object transform must be finite."));
    }
    const auto object = FindObject(m_document.m_objects, command.object);
    if (object == m_document.m_objects.end())
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.object_not_found", "Scene object does not exist."));
    }
    if (object->localTransform == command.localTransform)
    {
        return Result<SceneCommandResult>::Success(SceneCommandResult{
            object->id, m_document.m_revision, m_document.m_state, DocumentChangeKind::TransformChanged, {}, false});
    }

    SceneCommandDelta delta = TransformedObjectDelta{object->id, object->localTransform, command.localTransform};
    const std::size_t memoryBytes = EstimateMemoryBytes(delta, 1);
    const DocumentStateId beforeState = m_document.m_state;
    ApplyDelta(m_document.m_objects, delta);
    ++m_document.m_revision.value;
    m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
    std::vector affected{object->id};
    PushHistory(*m_history.m_impl,
                HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
    return Result<SceneCommandResult>::Success(
        SceneCommandResult{command.object, m_document.m_revision, m_document.m_state,
                           DocumentChangeKind::TransformChanged, std::move(affected), true});
}

/** @copydoc SceneDocumentCommandExecutor::Execute(const DuplicateSceneObjectCommand&) */
Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const DuplicateSceneObjectCommand &command)
{
    if (!IsValidName(command.name))
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.invalid_name", "Scene object name must contain 1 to 128 bytes."));
    }
    const auto source = FindObject(m_document.m_objects, command.source);
    if (source == m_document.m_objects.end())
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.object_not_found", "Scene object does not exist."));
    }

    const SceneObjectId id{m_document.m_nextObjectId};
    SceneCommandDelta delta = CreatedObjectDelta{
        .object = SceneObjectSnapshot{.id = id,
                                      .parent = source->parent,
                                      .name = command.name,
                                      .localTransform = source->localTransform,
                                      .primitiveMesh = source->primitiveMesh,
                                      .components = source->components},
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
    PushHistory(*m_history.m_impl,
                HistoryRecord{beforeState, m_document.m_state, std::move(delta), affected, memoryBytes});
    return Result<SceneCommandResult>::Success(SceneCommandResult{
        id, m_document.m_revision, m_document.m_state, DocumentChangeKind::Duplicated, std::move(affected), true});
}

/** @copydoc SceneDocumentCommandExecutor::Execute(const DeleteSceneObjectCommand&) */
Result<SceneCommandResult> SceneDocumentCommandExecutor::Execute(const DeleteSceneObjectCommand &command)
{
    if (FindObject(m_document.m_objects, command.object) == m_document.m_objects.end())
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.object_not_found", "Scene object does not exist."));
    }

    std::vector<SceneObjectId> removed{command.object};
    for (std::size_t index = 0; index < removed.size(); ++index)
    {
        for (const SceneObjectSnapshot &candidate : m_document.m_objects)
        {
            if (candidate.parent == removed[index])
            {
                removed.push_back(candidate.id);
            }
        }
    }
    DeletedObjectsDelta deleted{.root = command.object};
    deleted.objects.reserve(removed.size());
    for (std::size_t index = 0; index < m_document.m_objects.size(); ++index)
    {
        if (std::ranges::find(removed, m_document.m_objects[index].id) != removed.end())
        {
            deleted.objects.push_back(IndexedSceneObject{m_document.m_objects[index], index});
        }
    }
    SceneCommandDelta delta = std::move(deleted);
    if (const Result<void> validHistory = ValidateHistoryDelta(delta, removed.size()); validHistory.HasError())
    {
        return Result<SceneCommandResult>::Failure(validHistory.ErrorValue());
    }
    const std::size_t memoryBytes = EstimateMemoryBytes(delta, removed.size());
    const DocumentStateId beforeState = m_document.m_state;
    ApplyDelta(m_document.m_objects, delta);
    ++m_document.m_revision.value;
    m_document.m_state = DocumentStateId{m_document.m_nextStateId++};
    PushHistory(*m_history.m_impl,
                HistoryRecord{beforeState, m_document.m_state, std::move(delta), removed, memoryBytes});
    return Result<SceneCommandResult>::Success(SceneCommandResult{command.object, m_document.m_revision,
                                                                  m_document.m_state, DocumentChangeKind::Deleted,
                                                                  std::move(removed), true});
}

/** @copydoc SceneDocumentCommandExecutor::Undo */
Result<SceneCommandResult> SceneDocumentCommandExecutor::Undo()
{
    if (m_history.m_impl->undo.empty())
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.nothing_to_undo", "No committed scene command is available to undo."));
    }
    HistoryRecord entry = std::move(m_history.m_impl->undo.back());
    m_history.m_impl->undo.pop_back();
    RevertDelta(m_document.m_objects, entry.delta);
    ++m_document.m_revision.value;
    m_document.m_state = entry.beforeState;
    const SceneObjectId object = DeltaRootObject(entry.delta);
    std::vector affected = entry.affectedObjects;
    m_history.m_impl->redo.push_back(std::move(entry));
    return Result<SceneCommandResult>::Success(SceneCommandResult{
        object, m_document.m_revision, m_document.m_state, DocumentChangeKind::Undone, std::move(affected), true});
}

/** @copydoc SceneDocumentCommandExecutor::Redo */
Result<SceneCommandResult> SceneDocumentCommandExecutor::Redo()
{
    if (m_history.m_impl->redo.empty())
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.nothing_to_redo", "No reverted scene command is available to redo."));
    }
    HistoryRecord entry = std::move(m_history.m_impl->redo.back());
    m_history.m_impl->redo.pop_back();
    ApplyDelta(m_document.m_objects, entry.delta);
    ++m_document.m_revision.value;
    m_document.m_state = entry.afterState;
    const SceneObjectId object = DeltaRootObject(entry.delta);
    std::vector affected = entry.affectedObjects;
    m_history.m_impl->undo.push_back(std::move(entry));
    return Result<SceneCommandResult>::Success(SceneCommandResult{
        object, m_document.m_revision, m_document.m_state, DocumentChangeKind::Redone, std::move(affected), true});
}

/** @copydoc CreateSceneObjectUseCase::CreateSceneObjectUseCase */
CreateSceneObjectUseCase::CreateSceneObjectUseCase(SceneDocument &document,
                                                   SceneDocumentCommandExecutor &executor) noexcept
    : m_document(document), m_executor(executor)
{
}

/** @copydoc CreateSceneObjectUseCase::Execute */
Result<SceneCommandResult> CreateSceneObjectUseCase::Execute(const PrimitiveCreationRequest &request)
{
    const Runtime::PrimitiveDescriptor *descriptor = Runtime::PrimitiveCatalog::Find(request.primitive.value);
    if (descriptor == nullptr)
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.unknown_primitive", "Requested primitive is not registered."));
    }
    if (descriptor->creationGroup == Runtime::PrimitiveCreationGroup::NotCreatable ||
        descriptor->category == Runtime::PrimitiveCategory::Collider)
    {
        return Result<SceneCommandResult>::Failure(MakeDocumentError(
            "scene_document.primitive_not_creatable", "Requested primitive is not a hierarchy creation object."));
    }
    if (request.parent.has_value() && !m_document.Contains(*request.parent))
    {
        return Result<SceneCommandResult>::Failure(
            MakeDocumentError("scene_document.parent_not_found", "Scene object parent does not exist."));
    }

    std::string name{descriptor->defaultObjectName};
    const auto siblingHasName = [&](const std::string_view candidate) {
        return std::ranges::any_of(m_document.Objects(), [&](const SceneObjectSnapshot &object) {
            return object.parent == request.parent && object.name == candidate;
        });
    };
    for (std::uint32_t suffix = 2; siblingHasName(name); ++suffix)
    {
        name = std::format("{} {}", descriptor->defaultObjectName, suffix);
    }

    CreateSceneObjectCommand command{.name = std::move(name), .parent = request.parent};
    if (descriptor->meshType.has_value())
    {
        command.primitiveMesh = PrimitiveMeshDescriptor::Defaults(*descriptor->meshType);
    }
    else if (descriptor->sceneObjectType.has_value())
    {
        switch (*descriptor->sceneObjectType)
        {
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
    }
    else
    {
        return Result<SceneCommandResult>::Failure(MakeDocumentError(
            "scene_document.invalid_primitive_metadata", "Creatable primitive has no typed authoring descriptor."));
    }
    return m_executor.Execute(command);
}
} // namespace Horo::Editor
