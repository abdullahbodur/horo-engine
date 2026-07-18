#include "Horo/Runtime/Scene/RuntimeScene.h"
#include "RuntimeSceneErrors.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace Horo::Runtime
{
namespace
{
template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &code, std::string message)
{
    return Result<T>::Failure(MakeError(code, std::move(message)));
}
} // namespace

/** @copydoc SceneCommandBuffer::Create */
DeferredEntity SceneCommandBuffer::Create(RuntimeEntityCreateInfo createInfo)
{
    const DeferredEntity deferred{nextDeferred_++};
    commands_.push_back(CreateCommand{deferred, std::move(createInfo)});
    return deferred;
}

/** @copydoc SceneCommandBuffer::Destroy */
void SceneCommandBuffer::Destroy(EntityRef entity)
{
    commands_.push_back(DestroyCommand{entity});
}
/** @copydoc SceneCommandBuffer::Empty */
bool SceneCommandBuffer::Empty() const noexcept
{
    return commands_.empty();
}

/** @copydoc RuntimeSceneView::RuntimeSceneView */
RuntimeSceneView::RuntimeSceneView(const RuntimeScene &scene) noexcept : scene_(&scene)
{
}

/** @copydoc RuntimeSceneView::RuntimeId */
SceneRuntimeId RuntimeSceneView::RuntimeId() const noexcept
{
    return scene_ ? scene_->runtimeId_ : SceneRuntimeId{};
}

/** @copydoc RuntimeSceneView::DefinitionId */
SceneDefinitionId RuntimeSceneView::DefinitionId() const noexcept
{
    return scene_ ? scene_->definitionId_ : SceneDefinitionId{};
}

/** @copydoc RuntimeSceneView::DefinitionRevision */
SceneDefinitionRevision RuntimeSceneView::DefinitionRevision() const noexcept
{
    return scene_ ? scene_->definitionRevision_ : SceneDefinitionRevision{};
}

/** @copydoc RuntimeSceneView::SlotCount */
std::size_t RuntimeSceneView::SlotCount() const noexcept
{
    return scene_ ? scene_->slots_.size() : 0;
}
/** @copydoc RuntimeSceneView::EntityAt */
std::optional<RuntimeEntityView> RuntimeSceneView::EntityAt(const std::size_t slot) const noexcept
{
    if (!scene_ || slot >= scene_->slots_.size() || !scene_->slots_[slot].active)
        return std::nullopt;
    const RuntimeScene::Slot &value = scene_->slots_[slot];
    const EntityRef entity{scene_->runtimeId_, EntityId{static_cast<std::uint32_t>(slot), value.generation}};
    std::optional<EntityRef> parent;
    if (value.parent)
        parent = EntityRef{scene_->runtimeId_, *value.parent};
    return RuntimeEntityView{
        entity, value.authoredObject, parent, &value.localTransform, &value.primitiveMesh, &value.components};
}

/** @copydoc RuntimeSceneView::Find */
std::optional<EntityRef> RuntimeSceneView::Find(const SceneObjectId object) const noexcept
{
    if (!scene_ || !object.IsValid())
        return std::nullopt;
    const auto found = std::ranges::find(scene_->authoredIndex_, object, [](const auto &entry) { return entry.first; });
    if (found == scene_->authoredIndex_.end())
        return std::nullopt;
    return EntityRef{scene_->runtimeId_, found->second};
}

/** @copydoc RuntimeSceneView::Get */
Result<RuntimeEntityView> RuntimeSceneView::Get(const EntityRef entity) const
{
    if (!scene_ || !scene_->IsValid(entity))
        return Failure<RuntimeEntityView>(SceneErrors::StaleEntity,
                                          "Entity reference is stale or belongs to another runtime scene.");
    return Result<RuntimeEntityView>::Success(*EntityAt(entity.entity.index));
}

/** @copydoc RuntimeScene::RuntimeScene */
RuntimeScene::RuntimeScene(const SceneRuntimeId runtimeId, const SceneDefinitionId definitionId,
                           const SceneDefinitionRevision revision, const RuntimeSceneConfig config) noexcept
    : runtimeId_(runtimeId), definitionId_(definitionId), definitionRevision_(revision), config_(config)
{
}

/** @copydoc RuntimeScene::Create */
Result<std::unique_ptr<RuntimeScene>> RuntimeScene::Create(const RuntimeSceneDefinition &definition,
                                                           const SceneRuntimeId runtimeId,
                                                           const RuntimeSceneConfig config)
{
    if (!runtimeId.IsValid() || config.maximumGeneration == 0)
        return Failure<std::unique_ptr<RuntimeScene>>(SceneErrors::InvalidCandidate,
                                                      "Runtime identity and maximum generation must be non-zero.");
    auto scene =
        std::unique_ptr<RuntimeScene>(new RuntimeScene(runtimeId, definition.Id(), definition.Revision(), config));
    scene->slots_.reserve(definition.Entities().size());
    scene->authoredIndex_.reserve(definition.Entities().size());

    for (const RuntimeEntityDefinition &entity : definition.Entities())
    {
        RuntimeEntityCreateInfo info{entity.localTransform, std::nullopt, entity.object, entity.primitiveMesh,
                                     entity.components};
        Result<EntityRef> created = scene->CreateEntity(info);
        if (created.HasError())
            return Result<std::unique_ptr<RuntimeScene>>::Failure(created.ErrorValue());
    }
    for (const RuntimeEntityDefinition &entity : definition.Entities())
    {
        if (!entity.parent)
            continue;
        const std::optional<EntityRef> child = scene->View().Find(entity.object);
        const std::optional<EntityRef> parent = scene->View().Find(*entity.parent);
        if (!child || !parent)
            return Failure<std::unique_ptr<RuntimeScene>>(SceneErrors::ParentNotFound,
                                                          "Definition parent resolution failed during instantiation.");
        scene->slots_[child->entity.index].parent = parent->entity;
    }
    return Result<std::unique_ptr<RuntimeScene>>::Success(std::move(scene));
}

/** @copydoc RuntimeScene::View */
RuntimeSceneView RuntimeScene::View() const noexcept
{
    return RuntimeSceneView{*this};
}

Result<EntityRef> RuntimeScene::CreateEntity(const RuntimeEntityCreateInfo &info)
{
    RuntimeEntityDefinition validation{info.authoredObject.value_or(SceneObjectId{1}), std::nullopt,
                                       info.localTransform, info.primitiveMesh, info.components};
    const Result<void> valid = ValidateRuntimeEntityDefinition(validation);
    if (valid.HasError())
        return Result<EntityRef>::Failure(valid.ErrorValue());
    if (info.parent && !IsValid(*info.parent))
        return Failure<EntityRef>(SceneErrors::StaleEntity, "New entity parent reference is stale.");
    if (info.authoredObject && View().Find(*info.authoredObject))
        return Failure<EntityRef>(SceneErrors::DuplicateObject, "Runtime authored object identity already exists.");

    std::uint32_t index{};
    if (!freeList_.empty())
    {
        index = freeList_.back();
        freeList_.pop_back();
    }
    else
    {
        if (slots_.size() >= std::numeric_limits<std::uint32_t>::max())
            return Failure<EntityRef>(SceneErrors::StructuralCommitFailed,
                                      "Runtime entity slot capacity is exhausted.");
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.emplace_back();
    }
    Slot &slot = slots_[index];
    slot.active = true;
    slot.retired = false;
    slot.authoredObject = info.authoredObject;
    slot.parent = info.parent ? std::optional<EntityId>{info.parent->entity} : std::nullopt;
    slot.localTransform = info.localTransform;
    slot.primitiveMesh = info.primitiveMesh;
    slot.components = info.components;
    const EntityId id{index, slot.generation};
    if (slot.authoredObject)
        authoredIndex_.emplace_back(*slot.authoredObject, id);
    return Result<EntityRef>::Success(EntityRef{runtimeId_, id});
}

Result<void> RuntimeScene::DestroyEntity(const EntityRef entity)
{
    if (!IsValid(entity))
        return Result<void>::Failure(
            MakeError(SceneErrors::StaleEntity, "Destroyed entity reference is stale or belongs to another runtime."));
    if (std::ranges::any_of(
            slots_, [&](const Slot &slot) { return slot.active && slot.parent && *slot.parent == entity.entity; }))
        return Result<void>::Failure(
            MakeError(SceneErrors::StructuralCommitFailed, "Destroy children before destroying their runtime parent."));

    Slot &slot = slots_[entity.entity.index];
    if (slot.authoredObject)
        std::erase_if(authoredIndex_, [&](const auto &entry) { return entry.first == *slot.authoredObject; });
    slot.active = false;
    slot.authoredObject.reset();
    slot.parent.reset();
    slot.primitiveMesh.reset();
    slot.components = {};
    if (slot.generation == config_.maximumGeneration)
        slot.retired = true;
    else
    {
        ++slot.generation;
        freeList_.push_back(entity.entity.index);
    }
    return Result<void>::Success();
}

bool RuntimeScene::IsValid(const EntityRef entity) const noexcept
{
    return entity.runtime == runtimeId_ && entity.entity.IsValid() && entity.entity.index < slots_.size() &&
           slots_[entity.entity.index].active && slots_[entity.entity.index].generation == entity.entity.generation;
}

/** @copydoc RuntimeScene::Commit */
Result<StructuralCommitResult> RuntimeScene::Commit(SceneCommandBuffer commands)
{
    RuntimeScene candidate = *this;
    StructuralCommitResult result;
    result.created.reserve(commands.commands_.size());
    for (const SceneCommandBuffer::Command &command : commands.commands_)
    {
        if (const auto *create = std::get_if<SceneCommandBuffer::CreateCommand>(&command))
        {
            Result<EntityRef> created = candidate.CreateEntity(create->info);
            if (created.HasError())
                return Result<StructuralCommitResult>::Failure(created.ErrorValue());
            result.created.push_back({create->deferred, created.Value()});
        }
        else
        {
            const Result<void> destroyed =
                candidate.DestroyEntity(std::get<SceneCommandBuffer::DestroyCommand>(command).entity);
            if (destroyed.HasError())
                return Result<StructuralCommitResult>::Failure(destroyed.ErrorValue());
            ++result.destroyed;
        }
    }
    *this = std::move(candidate);
    return Result<StructuralCommitResult>::Success(std::move(result));
}

/** @copydoc RuntimeSceneService::Prepare */
Result<std::unique_ptr<RuntimeScene>> RuntimeSceneService::Prepare(const RuntimeSceneDefinition &definition,
                                                                   const RuntimeSceneConfig config)
{
    if (nextRuntimeId_ == 0)
        return Failure<std::unique_ptr<RuntimeScene>>(SceneErrors::InvalidCandidate,
                                                      "Runtime scene identity space is exhausted.");
    return RuntimeScene::Create(definition, SceneRuntimeId{nextRuntimeId_++}, config);
}

/** @copydoc RuntimeSceneService::QueueActivation */
Result<void> RuntimeSceneService::QueueActivation(std::unique_ptr<RuntimeScene> candidate)
{
    if (!candidate)
        return Result<void>::Failure(MakeError(SceneErrors::InvalidCandidate, "Scene candidate must be non-null."));
    if (transition_ != TransitionKind::None || structuralCommands_)
        return Result<void>::Failure(MakeError(SceneErrors::OperationInProgress));
    pending_ = std::move(candidate);
    transition_ = TransitionKind::Activate;
    return Result<void>::Success();
}

/** @copydoc RuntimeSceneService::QueueUnload */
Result<void> RuntimeSceneService::QueueUnload()
{
    if (transition_ == TransitionKind::Unload)
        return Result<void>::Success();
    if (transition_ != TransitionKind::None || structuralCommands_)
        return Result<void>::Failure(MakeError(SceneErrors::OperationInProgress));
    if (!active_)
        return Result<void>::Success();
    transition_ = TransitionKind::Unload;
    return Result<void>::Success();
}

/** @copydoc RuntimeSceneService::QueueStructuralCommands */
Result<void> RuntimeSceneService::QueueStructuralCommands(SceneCommandBuffer commands)
{
    if (!active_)
        return Result<void>::Failure(MakeError(SceneErrors::NoActiveScene));
    if (transition_ != TransitionKind::None || structuralCommands_)
        return Result<void>::Failure(MakeError(SceneErrors::OperationInProgress));
    if (!commands.Empty())
        structuralCommands_ = std::move(commands);
    return Result<void>::Success();
}

/** @copydoc RuntimeSceneService::ActiveScene */
std::optional<RuntimeSceneView> RuntimeSceneService::ActiveScene() const noexcept
{
    if (!active_)
        return std::nullopt;
    return active_->View();
}

/** @copydoc RuntimeSceneService::TakeStructuralCommitResult */
std::optional<StructuralCommitResult> RuntimeSceneService::TakeStructuralCommitResult()
{
    return std::exchange(structuralResult_, std::nullopt);
}

/** @copydoc RuntimeSceneService::TakeOperationError */
std::optional<Error> RuntimeSceneService::TakeOperationError()
{
    return std::exchange(operationError_, std::nullopt);
}

/** @copydoc RuntimeSceneService::Startup */
Result<void> RuntimeSceneService::Startup(const CancellationToken &cancellation)
{
    if (cancellation.IsCancellationRequested())
        return Result<void>::Failure(MakeError(SceneErrors::InvalidCandidate, "Scene service startup was cancelled."));
    started_ = true;
    return Result<void>::Success();
}

/** @copydoc RuntimeSceneService::OnPhase */
Result<void> RuntimeSceneService::OnPhase(const RuntimePhase phase, const FrameContext &)
{
    if (phase == RuntimePhase::CommitDeferredLifecycleChanges)
        return CommitDeferredChanges();
    return Result<void>::Success();
}

/** @copydoc RuntimeSceneService::OnFixedUpdate */
Result<void> RuntimeSceneService::OnFixedUpdate(const FixedStepContext &)
{
    return Result<void>::Success();
}
/** @copydoc RuntimeSceneService::Shutdown */
void RuntimeSceneService::Shutdown() noexcept
{
    structuralCommands_.reset();
    structuralResult_.reset();
    operationError_.reset();
    pending_.reset();
    active_.reset();
    transition_ = TransitionKind::None;
    started_ = false;
}

Result<void> RuntimeSceneService::CommitDeferredChanges()
{
    if (structuralCommands_)
    {
        Result<StructuralCommitResult> committed = active_->Commit(std::move(*structuralCommands_));
        structuralCommands_.reset();
        if (committed.HasError())
            operationError_ = committed.ErrorValue();
        else
            structuralResult_ = std::move(committed).Value();
    }
    if (transition_ == TransitionKind::Activate)
        active_ = std::move(pending_);
    else if (transition_ == TransitionKind::Unload)
        active_.reset();
    transition_ = TransitionKind::None;
    return Result<void>::Success();
}
} // namespace Horo::Runtime
