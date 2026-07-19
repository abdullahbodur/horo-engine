#include "Horo/Runtime/Scene/RuntimeScene.h"
#include "Horo/Assets/AssetProvider.h"
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

[[nodiscard]] bool IsTerminal(const Assets::AssetLoadState state) noexcept
{
    return state == Assets::AssetLoadState::Succeeded || state == Assets::AssetLoadState::Failed ||
           state == Assets::AssetLoadState::Cancelled;
}

[[nodiscard]] Error WithAssetContext(Error error, const SceneDefinitionId scene, const Assets::AssetId asset)
{
    error.diagnostics.push_back(
        Diagnostic{DiagnosticCode{"scene.asset.context"}, DiagnosticSeverity::Note,
                   "Scene " + std::to_string(scene.value) + " requires asset " + asset.ToString() + ".",
                   SourceLocation{asset.ToString(), 0, 0}});
    return error;
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
RuntimeSceneView::RuntimeSceneView(const RuntimeScene &scene) noexcept
    : scene_(&scene), structuralRevision_(scene.structuralRevision_)
{
}

/** @copydoc RuntimeSceneView::IsCurrent */
bool RuntimeSceneView::IsCurrent() const noexcept
{
    return scene_ != nullptr && structuralRevision_ == scene_->structuralRevision_;
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

/** @copydoc RuntimeSceneView::AssetRegistryRevision */
Assets::AssetRegistryRevision RuntimeSceneView::AssetRegistryRevision() const noexcept
{
    return scene_ ? scene_->assetRegistryRevision_ : Assets::AssetRegistryRevision{};
}

/** @copydoc RuntimeSceneView::SlotCount */
std::size_t RuntimeSceneView::SlotCount() const noexcept
{
    return IsCurrent() ? scene_->storage_.slots.size() : 0;
}
/** @copydoc RuntimeSceneView::EntityAt */
std::optional<RuntimeEntityView> RuntimeSceneView::EntityAt(const std::size_t slot) const noexcept
{
    if (!IsCurrent() || slot >= scene_->storage_.slots.size() || !scene_->storage_.slots[slot].active)
        return std::nullopt;
    const RuntimeScene::Slot &value = scene_->storage_.slots[slot];
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
    if (!IsCurrent() || !object.IsValid())
        return std::nullopt;
    const auto found =
        std::ranges::find(scene_->storage_.authoredIndex, object, [](const auto &entry) { return entry.first; });
    if (found == scene_->storage_.authoredIndex.end())
        return std::nullopt;
    return EntityRef{scene_->runtimeId_, found->second};
}

/** @copydoc RuntimeSceneView::FindAsset */
std::optional<RuntimeSceneAssetView> RuntimeSceneView::FindAsset(const Assets::AssetId id) const noexcept
{
    if (!IsCurrent() || !id.IsValid())
        return std::nullopt;
    const auto found = std::ranges::find(scene_->assets_, id,
                                         [](const RuntimeScene::ResolvedAsset &asset) { return asset.dependency.id; });
    if (found == scene_->assets_.end() || !found->payload)
        return std::nullopt;
    return RuntimeSceneAssetView{found->dependency.id, &found->dependency.expectedType,
                                 std::span<const std::uint8_t>{*found->payload}};
}

/** @copydoc RuntimeSceneView::Get */
Result<RuntimeEntityView> RuntimeSceneView::Get(const EntityRef entity) const
{
    if (!IsCurrent())
        return Failure<RuntimeEntityView>(SceneErrors::StaleView, "Scene view was invalidated by a structural commit.");
    if (!scene_->IsValid(scene_->storage_, entity))
        return Failure<RuntimeEntityView>(SceneErrors::StaleEntity,
                                          "Entity reference is stale or belongs to another runtime scene.");
    return Result<RuntimeEntityView>::Success(*EntityAt(entity.entity.index));
}

/** @copydoc RuntimeScene::RuntimeScene */
RuntimeScene::RuntimeScene(const SceneRuntimeId runtimeId, const SceneDefinitionId definitionId,
                           const SceneDefinitionRevision revision, const RuntimeSceneConfig config,
                           const Assets::AssetRegistryRevision assetRevision,
                           std::vector<ResolvedAsset> assets) noexcept
    : runtimeId_(runtimeId), definitionId_(definitionId), definitionRevision_(revision),
      assetRegistryRevision_(assetRevision), assets_(std::move(assets)), config_(config)
{
}

/** @copydoc RuntimeScene::Create */
Result<std::unique_ptr<RuntimeScene>> RuntimeScene::Create(const RuntimeSceneDefinition &definition,
                                                           const SceneRuntimeId runtimeId,
                                                           const RuntimeSceneConfig config)
{
    if (!definition.AssetDependencies().empty())
        return Failure<std::unique_ptr<RuntimeScene>>(
            SceneErrors::AssetServicesUnavailable,
            "Asset-bearing definitions must be prepared through RuntimeSceneService.");
    return CreateResolved(definition, runtimeId, config, {}, {});
}

Result<std::unique_ptr<RuntimeScene>> RuntimeScene::CreateResolved(const RuntimeSceneDefinition &definition,
                                                                   const SceneRuntimeId runtimeId,
                                                                   const RuntimeSceneConfig config,
                                                                   const Assets::AssetRegistryRevision assetRevision,
                                                                   std::vector<ResolvedAsset> assets)
{
    if (!runtimeId.IsValid() || config.maximumGeneration == 0)
        return Failure<std::unique_ptr<RuntimeScene>>(SceneErrors::InvalidCandidate,
                                                      "Runtime identity and maximum generation must be non-zero.");
    if (assets.size() != definition.AssetDependencies().size())
        return Failure<std::unique_ptr<RuntimeScene>>(SceneErrors::InvalidCandidate,
                                                      "Resolved asset set does not match the definition.");
    auto scene = std::unique_ptr<RuntimeScene>(
        new RuntimeScene(runtimeId, definition.Id(), definition.Revision(), config, assetRevision, std::move(assets)));
    scene->storage_.slots.reserve(definition.Entities().size());
    scene->storage_.authoredIndex.reserve(definition.Entities().size());

    for (const RuntimeEntityDefinition &entity : definition.Entities())
    {
        RuntimeEntityCreateInfo info{entity.localTransform, std::nullopt, entity.object, entity.primitiveMesh,
                                     entity.components};
        Result<EntityRef> created = scene->CreateEntity(scene->storage_, info);
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
        scene->storage_.slots[child->entity.index].parent = parent->entity;
    }
    return Result<std::unique_ptr<RuntimeScene>>::Success(std::move(scene));
}

/** @copydoc RuntimeScene::View */
RuntimeSceneView RuntimeScene::View() const noexcept
{
    return RuntimeSceneView{*this};
}

Result<EntityRef> RuntimeScene::CreateEntity(RuntimeSceneStorage &storage, const RuntimeEntityCreateInfo &info) const
{
    RuntimeEntityDefinition validation{info.authoredObject.value_or(SceneObjectId{1}), std::nullopt,
                                       info.localTransform, info.primitiveMesh, info.components};
    const Result<void> valid = ValidateRuntimeEntityDefinition(validation);
    if (valid.HasError())
        return Result<EntityRef>::Failure(valid.ErrorValue());
    if (info.parent && !IsValid(storage, *info.parent))
        return Failure<EntityRef>(SceneErrors::StaleEntity, "New entity parent reference is stale.");
    if (info.authoredObject && std::ranges::find(storage.authoredIndex, *info.authoredObject, [](const auto &entry) {
                                   return entry.first;
                               }) != storage.authoredIndex.end())
        return Failure<EntityRef>(SceneErrors::DuplicateObject, "Runtime authored object identity already exists.");

    std::uint32_t index{};
    if (!storage.freeList.empty())
    {
        index = storage.freeList.back();
        storage.freeList.pop_back();
    }
    else
    {
        if (storage.slots.size() >= std::numeric_limits<std::uint32_t>::max())
            return Failure<EntityRef>(SceneErrors::StructuralCommitFailed,
                                      "Runtime entity slot capacity is exhausted.");
        index = static_cast<std::uint32_t>(storage.slots.size());
        storage.slots.emplace_back();
    }
    Slot &slot = storage.slots[index];
    slot.active = true;
    slot.retired = false;
    slot.authoredObject = info.authoredObject;
    slot.parent = info.parent ? std::optional<EntityId>{info.parent->entity} : std::nullopt;
    slot.localTransform = info.localTransform;
    slot.primitiveMesh = info.primitiveMesh;
    slot.components = info.components;
    const EntityId id{index, slot.generation};
    if (slot.authoredObject)
        storage.authoredIndex.emplace_back(*slot.authoredObject, id);
    return Result<EntityRef>::Success(EntityRef{runtimeId_, id});
}

Result<void> RuntimeScene::DestroyEntity(RuntimeSceneStorage &storage, const EntityRef entity) const
{
    if (!IsValid(storage, entity))
        return Result<void>::Failure(
            MakeError(SceneErrors::StaleEntity, "Destroyed entity reference is stale or belongs to another runtime."));
    if (std::ranges::any_of(storage.slots, [&](const Slot &slot) {
            return slot.active && slot.parent && *slot.parent == entity.entity;
        }))
        return Result<void>::Failure(
            MakeError(SceneErrors::StructuralCommitFailed, "Destroy children before destroying their runtime parent."));

    Slot &slot = storage.slots[entity.entity.index];
    if (slot.authoredObject)
        std::erase_if(storage.authoredIndex, [&](const auto &entry) { return entry.first == *slot.authoredObject; });
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
        storage.freeList.push_back(entity.entity.index);
    }
    return Result<void>::Success();
}

bool RuntimeScene::IsValid(const RuntimeSceneStorage &storage, const EntityRef entity) const noexcept
{
    return entity.runtime == runtimeId_ && entity.entity.IsValid() && entity.entity.index < storage.slots.size() &&
           storage.slots[entity.entity.index].active &&
           storage.slots[entity.entity.index].generation == entity.entity.generation;
}

/** @copydoc RuntimeScene::Commit */
Result<StructuralCommitResult> RuntimeScene::Commit(SceneCommandBuffer commands)
{
    if (commands.Empty())
        return Result<StructuralCommitResult>::Success({});
    RuntimeSceneStorage candidate = storage_;
    StructuralCommitResult result;
    result.created.reserve(commands.commands_.size());
    for (const SceneCommandBuffer::Command &command : commands.commands_)
    {
        if (const auto *create = std::get_if<SceneCommandBuffer::CreateCommand>(&command))
        {
            Result<EntityRef> created = CreateEntity(candidate, create->info);
            if (created.HasError())
                return Result<StructuralCommitResult>::Failure(created.ErrorValue());
            result.created.push_back({create->deferred, created.Value()});
        }
        else
        {
            const Result<void> destroyed =
                DestroyEntity(candidate, std::get<SceneCommandBuffer::DestroyCommand>(command).entity);
            if (destroyed.HasError())
                return Result<StructuralCommitResult>::Failure(destroyed.ErrorValue());
            ++result.destroyed;
        }
    }
    storage_ = std::move(candidate);
    ++structuralRevision_;
    return Result<StructuralCommitResult>::Success(std::move(result));
}

struct RuntimeSceneService::Preparation
{
    struct Entry
    {
        SceneAssetDependency dependency;
        std::shared_ptr<const std::vector<std::uint8_t>> payload;
        std::optional<Assets::AssetLoadHandle> load;
    };

    Preparation(const RuntimeSceneDefinition &source, const RuntimeSceneConfig sceneConfig,
                Assets::AssetRegistrySnapshot registrySnapshot)
        : definition(source), config(sceneConfig), snapshot(std::move(registrySnapshot))
    {
    }

    RuntimeSceneDefinition definition;
    RuntimeSceneConfig config;
    Assets::AssetRegistrySnapshot snapshot;
    std::vector<Entry> entries;
    std::size_t activeLoads{};
    std::size_t residentBytes{};
};

RuntimeSceneService::RuntimeSceneService() = default;

RuntimeSceneService::RuntimeSceneService(Assets::AssetRegistry &registry, Assets::AssetLoadService &loads,
                                         const RuntimeSceneAssetLimits limits)
    : assetRegistry_(&registry), assetLoads_(&loads), assetLimits_(limits)
{
}

RuntimeSceneService::~RuntimeSceneService()
{
    Shutdown();
}

/** @copydoc RuntimeSceneService::QueuePreparation */
Result<void> RuntimeSceneService::QueuePreparation(const RuntimeSceneDefinition &definition,
                                                   const RuntimeSceneConfig config)
{
    if (shutdown_)
        return Result<void>::Failure(MakeError(SceneErrors::ServiceShutdown));
    if (transition_ != TransitionKind::None || structuralCommands_ || preparation_)
        return Result<void>::Failure(MakeError(SceneErrors::OperationInProgress));
    operationError_.reset();
    return BeginPreparation(definition, config);
}

Result<void> RuntimeSceneService::BeginPreparation(const RuntimeSceneDefinition &definition,
                                                   const RuntimeSceneConfig config)
{
    if (nextRuntimeId_ == 0)
        return Result<void>::Failure(
            MakeError(SceneErrors::InvalidCandidate, "Runtime scene identity space is exhausted."));
    if (config.maximumGeneration == 0 || assetLimits_.maximumDependencies == 0 ||
        assetLimits_.maximumConcurrentLoads == 0 || assetLimits_.maximumResidentBytes == 0)
        return Result<void>::Failure(MakeError(SceneErrors::AssetLimitsInvalid));
    if (definition.AssetDependencies().size() > assetLimits_.maximumDependencies)
        return Result<void>::Failure(MakeError(SceneErrors::AssetBudgetExceeded,
                                               "Runtime scene dependency count exceeds the configured limit."));

    if (definition.AssetDependencies().empty())
    {
        Result<std::unique_ptr<RuntimeScene>> candidate =
            RuntimeScene::CreateResolved(definition, SceneRuntimeId{nextRuntimeId_}, config, {}, {});
        if (candidate.HasError())
            return Result<void>::Failure(candidate.ErrorValue());
        ++nextRuntimeId_;
        pending_ = std::move(candidate).Value();
        transition_ = TransitionKind::Activate;
        return Result<void>::Success();
    }
    if (!assetRegistry_ || !assetLoads_)
        return Result<void>::Failure(MakeError(SceneErrors::AssetServicesUnavailable));

    Assets::AssetRegistrySnapshot snapshot = assetRegistry_->Snapshot();
    for (const SceneAssetDependency &dependency : definition.AssetDependencies())
    {
        const Assets::AssetRecord *record = snapshot.Find(dependency.id);
        if (!record)
            return Result<void>::Failure(
                WithAssetContext(MakeError(SceneErrors::AssetMissing), definition.Id(), dependency.id));
        if (record->type != dependency.expectedType)
            return Result<void>::Failure(
                WithAssetContext(MakeError(SceneErrors::AssetTypeMismatch), definition.Id(), dependency.id));
    }

    preparation_ = std::make_unique<Preparation>(definition, config, std::move(snapshot));
    preparation_->entries.reserve(definition.AssetDependencies().size());
    for (const SceneAssetDependency &dependency : definition.AssetDependencies())
    {
        Preparation::Entry entry{dependency};
        if (active_ && active_->assetRegistryRevision_ == preparation_->snapshot.Revision())
        {
            const auto reusable =
                std::ranges::find(active_->assets_, dependency.id,
                                  [](const RuntimeScene::ResolvedAsset &asset) { return asset.dependency.id; });
            if (reusable != active_->assets_.end() && reusable->dependency.expectedType == dependency.expectedType)
                entry.payload = reusable->payload;
        }
        if (entry.payload)
        {
            if (entry.payload->size() > assetLimits_.maximumResidentBytes - preparation_->residentBytes)
            {
                preparation_.reset();
                return Result<void>::Failure(
                    WithAssetContext(MakeError(SceneErrors::AssetBudgetExceeded), definition.Id(), dependency.id));
            }
            preparation_->residentBytes += entry.payload->size();
        }
        preparation_->entries.push_back(std::move(entry));
    }
    const Result<void> submitted = SubmitPreparationLoads();
    if (submitted.HasError())
    {
        const Error error = submitted.ErrorValue();
        CancelPreparation(false);
        return Result<void>::Failure(error);
    }
    return Result<void>::Success();
}

/** @copydoc RuntimeSceneService::QueueUnload */
Result<void> RuntimeSceneService::QueueUnload()
{
    if (transition_ == TransitionKind::Unload)
        return Result<void>::Success();
    if (structuralCommands_)
        return Result<void>::Failure(MakeError(SceneErrors::OperationInProgress));
    CancelPreparation(false);
    pending_.reset();
    transition_ = TransitionKind::None;
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
    if (transition_ != TransitionKind::None || structuralCommands_ || preparation_)
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
    shutdown_ = false;
    return Result<void>::Success();
}

/** @copydoc RuntimeSceneService::OnPhase */
Result<void> RuntimeSceneService::OnPhase(const RuntimePhase phase, const FrameContext &)
{
    if (phase == RuntimePhase::CommitDeferredLifecycleChanges)
    {
        AdvancePreparation();
        return CommitDeferredChanges();
    }
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
    CancelPreparation(true);
    structuralCommands_.reset();
    structuralResult_.reset();
    operationError_.reset();
    pending_.reset();
    active_.reset();
    transition_ = TransitionKind::None;
    started_ = false;
    shutdown_ = true;
}

Result<void> RuntimeSceneService::SubmitPreparationLoads()
{
    if (!preparation_ || !assetLoads_)
        return Result<void>::Success();
    for (Preparation::Entry &entry : preparation_->entries)
    {
        if (preparation_->activeLoads >= assetLimits_.maximumConcurrentLoads)
            break;
        if (entry.payload || entry.load)
            continue;
        Result<Assets::AssetLoadHandle> submitted = assetLoads_->LoadAsync(preparation_->snapshot, entry.dependency.id);
        if (submitted.HasError())
            return Result<void>::Failure(
                WithAssetContext(submitted.ErrorValue(), preparation_->definition.Id(), entry.dependency.id));
        entry.load = std::move(submitted).Value();
        ++preparation_->activeLoads;
    }
    return Result<void>::Success();
}

void RuntimeSceneService::AdvancePreparation()
{
    if (!preparation_)
        return;

    for (Preparation::Entry &entry : preparation_->entries)
    {
        if (!entry.load || !IsTerminal(entry.load->State()))
            continue;
        Result<Assets::AssetLoadResult> loaded = entry.load->TakeResult();
        entry.load.reset();
        --preparation_->activeLoads;
        if (loaded.HasError())
        {
            Error error = WithAssetContext(loaded.ErrorValue(), preparation_->definition.Id(), entry.dependency.id);
            CancelPreparation(false);
            operationError_ = std::move(error);
            return;
        }
        Assets::AssetLoadResult result = std::move(loaded).Value();
        if (result.sourceRegistryRevision != preparation_->snapshot.Revision())
        {
            Error error = WithAssetContext(MakeError(SceneErrors::AssetRevisionStale), preparation_->definition.Id(),
                                           entry.dependency.id);
            CancelPreparation(false);
            operationError_ = std::move(error);
            return;
        }
        if (result.bytes.empty())
        {
            Error error = WithAssetContext(MakeError(SceneErrors::AssetPayloadEmpty), preparation_->definition.Id(),
                                           entry.dependency.id);
            CancelPreparation(false);
            operationError_ = std::move(error);
            return;
        }
        if (result.bytes.size() > assetLimits_.maximumResidentBytes - preparation_->residentBytes)
        {
            Error error = WithAssetContext(MakeError(SceneErrors::AssetBudgetExceeded), preparation_->definition.Id(),
                                           entry.dependency.id);
            CancelPreparation(false);
            operationError_ = std::move(error);
            return;
        }
        preparation_->residentBytes += result.bytes.size();
        entry.payload = std::make_shared<const std::vector<std::uint8_t>>(std::move(result.bytes));
    }

    const Result<void> submitted = SubmitPreparationLoads();
    if (submitted.HasError())
    {
        Error error = submitted.ErrorValue();
        CancelPreparation(false);
        operationError_ = std::move(error);
        return;
    }
    if (preparation_->activeLoads != 0 ||
        std::ranges::any_of(preparation_->entries, [](const Preparation::Entry &entry) { return !entry.payload; }))
        return;

    if (!assetRegistry_ || assetRegistry_->Snapshot().Revision() != preparation_->snapshot.Revision())
    {
        Error error = MakeError(SceneErrors::AssetRevisionStale);
        error.diagnostics.push_back(Diagnostic{DiagnosticCode{"scene.asset.registry_revision"},
                                               DiagnosticSeverity::Note,
                                               "The authoritative registry revision changed before activation.",
                                               {}});
        CancelPreparation(false);
        operationError_ = std::move(error);
        return;
    }

    std::vector<RuntimeScene::ResolvedAsset> assets;
    assets.reserve(preparation_->entries.size());
    for (Preparation::Entry &entry : preparation_->entries)
        assets.push_back(RuntimeScene::ResolvedAsset{std::move(entry.dependency), std::move(entry.payload)});
    Result<std::unique_ptr<RuntimeScene>> candidate =
        RuntimeScene::CreateResolved(preparation_->definition, SceneRuntimeId{nextRuntimeId_}, preparation_->config,
                                     preparation_->snapshot.Revision(), std::move(assets));
    if (candidate.HasError())
    {
        operationError_ = candidate.ErrorValue();
        preparation_.reset();
        return;
    }
    ++nextRuntimeId_;
    pending_ = std::move(candidate).Value();
    preparation_.reset();
    transition_ = TransitionKind::Activate;
}

void RuntimeSceneService::CancelPreparation(const bool waitForCompletion) noexcept
{
    if (!preparation_)
        return;
    for (Preparation::Entry &entry : preparation_->entries)
        if (entry.load)
            static_cast<void>(entry.load->RequestCancel());
    if (waitForCompletion)
        for (Preparation::Entry &entry : preparation_->entries)
            if (entry.load)
                static_cast<void>(entry.load->Wait());
    preparation_.reset();
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
