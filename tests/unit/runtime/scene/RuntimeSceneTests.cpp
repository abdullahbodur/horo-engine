#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"

#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>

namespace
{
std::atomic<std::size_t> gAllocations{};
void *Allocate(std::size_t size)
{
    gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void *memory = std::malloc(size))
        return memory;
    throw std::bad_alloc{};
}
} // namespace

void *operator new(std::size_t size)
{
    return Allocate(size);
}
void *operator new[](std::size_t size)
{
    return Allocate(size);
}
void operator delete(void *memory) noexcept
{
    std::free(memory);
}
void operator delete[](void *memory) noexcept
{
    std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}
void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace
{
using namespace Horo;
using namespace Horo::Runtime;

void Check(bool condition)
{
    if (!condition)
        std::abort();
}

RuntimeEntityDefinition Entity(std::uint64_t id, std::optional<std::uint64_t> parent = std::nullopt)
{
    RuntimeEntityDefinition entity;
    entity.object = SceneObjectId{id};
    if (parent)
        entity.parent = SceneObjectId{*parent};
    return entity;
}

RuntimeSceneDefinition Definition(std::uint64_t revision = 1)
{
    SceneDefinitionBuilder builder{SceneDefinitionId{7}, SceneDefinitionRevision{revision}};
    builder.Add(Entity(1));
    builder.Add(Entity(2, 1));
    auto built = std::move(builder).Build();
    Check(built.HasValue());
    return std::move(built).Value();
}

FrameContext Context(const CancellationToken &token)
{
    return FrameContext{1, {}, 0.0, 0, {}, false, token};
}

void DefinitionValidation()
{
    SceneDefinitionBuilder duplicate{SceneDefinitionId{1}, {}};
    duplicate.Add(Entity(1));
    duplicate.Add(Entity(1));
    Check(std::move(duplicate).Build().HasError());

    SceneDefinitionBuilder missing{SceneDefinitionId{1}, {}};
    missing.Add(Entity(1, 99));
    Check(std::move(missing).Build().HasError());

    SceneDefinitionBuilder cycle{SceneDefinitionId{1}, {}};
    cycle.Add(Entity(1, 2));
    cycle.Add(Entity(2, 1));
    Check(std::move(cycle).Build().HasError());

    RuntimeEntityDefinition invalid = Entity(1);
    invalid.localTransform.translation.x = std::numeric_limits<float>::infinity();
    SceneDefinitionBuilder numeric{SceneDefinitionId{1}, {}};
    numeric.Add(std::move(invalid));
    Check(std::move(numeric).Build().HasError());
}

void IdentityAndTransactionalCommands()
{
    auto created = RuntimeScene::Create(Definition(), SceneRuntimeId{10});
    Check(created.HasValue());
    std::unique_ptr<RuntimeScene> scene = std::move(created).Value();
    const RuntimeSceneView initial = scene->View();
    const EntityRef root = *initial.Find(SceneObjectId{1});
    Check(initial.Get(root).HasValue());
    Check(initial.Get(EntityRef{SceneRuntimeId{11}, root.entity}).HasError());

    SceneCommandBuffer invalidBatch;
    [[maybe_unused]] const DeferredEntity invalidCreate = invalidBatch.Create(RuntimeEntityCreateInfo{});
    invalidBatch.Destroy(EntityRef{SceneRuntimeId{10}, EntityId{99, 1}});
    Check(scene->Commit(std::move(invalidBatch)).HasError());
    Check(scene->View().SlotCount() == 2);

    SceneCommandBuffer destroyChild;
    destroyChild.Destroy(*scene->View().Find(SceneObjectId{2}));
    auto childDestroyed = scene->Commit(std::move(destroyChild));
    Check(childDestroyed.HasValue());
    Check(scene->View().Get(root).HasValue());

    SceneCommandBuffer destroyRoot;
    destroyRoot.Destroy(root);
    Check(scene->Commit(std::move(destroyRoot)).HasValue());
    Check(scene->View().Get(root).HasError());

    SceneCommandBuffer createTwo;
    RuntimeEntityCreateInfo fullCreate;
    fullCreate.localTransform.translation = {3.0F, 4.0F, 5.0F};
    fullCreate.primitiveMesh = PrimitiveMeshDescriptor{};
    fullCreate.components.camera = CameraComponent{};
    const DeferredEntity first = createTwo.Create(fullCreate);
    const DeferredEntity second = createTwo.Create(RuntimeEntityCreateInfo{});
    auto createdTwo = scene->Commit(std::move(createTwo));
    Check(createdTwo.HasValue());
    const EntityRef firstRef = createdTwo.Value().created[0].entity;
    const EntityRef secondRef = createdTwo.Value().created[1].entity;
    Check(firstRef.entity.index == 0);
    Check(secondRef.entity.index == 1);
    Check(createdTwo.Value().created[0].deferred == first);
    Check(createdTwo.Value().created[1].deferred == second);
    const RuntimeEntityView fullView = scene->View().Get(firstRef).Value();
    Check(fullView.localTransform->translation == Math::Vec3{3.0F, 4.0F, 5.0F});
    Check(fullView.primitiveMesh->has_value());
    Check(fullView.components->camera.has_value());

    SceneCommandBuffer destroyTwo;
    destroyTwo.Destroy(firstRef);
    destroyTwo.Destroy(secondRef);
    Check(scene->Commit(std::move(destroyTwo)).HasValue());
    SceneCommandBuffer reuse;
    const DeferredEntity reused = reuse.Create(RuntimeEntityCreateInfo{});
    auto reuseResult = scene->Commit(std::move(reuse));
    Check(reuseResult.HasValue());
    Check(reuseResult.Value().created[0].deferred == reused);
    Check(reuseResult.Value().created[0].entity.entity.index == secondRef.entity.index);
    Check(reuseResult.Value().created[0].entity.entity.generation != secondRef.entity.generation);
    Check(first.IsValid() && second.IsValid());
}

void GenerationRetirement()
{
    SceneDefinitionBuilder builder{SceneDefinitionId{1}, {}};
    builder.Add(Entity(1));
    auto definition = std::move(builder).Build();
    Check(definition.HasValue());
    auto created = RuntimeScene::Create(definition.Value(), SceneRuntimeId{2}, RuntimeSceneConfig{2});
    Check(created.HasValue());
    std::unique_ptr<RuntimeScene> scene = std::move(created).Value();
    EntityRef entity = *scene->View().Find(SceneObjectId{1});

    SceneCommandBuffer destroy;
    destroy.Destroy(entity);
    Check(scene->Commit(std::move(destroy)).HasValue());
    SceneCommandBuffer recreate;
    [[maybe_unused]] const DeferredEntity recreated = recreate.Create(RuntimeEntityCreateInfo{});
    auto second = scene->Commit(std::move(recreate));
    Check(second.HasValue());
    entity = second.Value().created[0].entity;
    Check(entity.entity.index == 0 && entity.entity.generation == 2);

    SceneCommandBuffer retire;
    retire.Destroy(entity);
    Check(scene->Commit(std::move(retire)).HasValue());
    SceneCommandBuffer afterRetire;
    [[maybe_unused]] const DeferredEntity createdAfterRetirement = afterRetire.Create(RuntimeEntityCreateInfo{});
    auto next = scene->Commit(std::move(afterRetire));
    Check(next.HasValue());
    Check(next.Value().created[0].entity.entity.index == 1);
}

void LifecycleAndSteadyState()
{
    RuntimeSceneService service;
    CancellationSource cancellation;
    const CancellationToken token = cancellation.Token();
    Check(service.Startup(token).HasValue());
    auto candidate = service.Prepare(Definition());
    Check(candidate.HasValue());
    const SceneRuntimeId preparedId = candidate.Value()->View().RuntimeId();
    Check(service.QueueActivation(std::move(candidate).Value()).HasValue());
    Check(!service.ActiveScene());
    Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(token)).HasValue());
    Check(service.ActiveScene()->RuntimeId() == preparedId);
    const EntityRef oldEntity = *service.ActiveScene()->Find(SceneObjectId{1});

    auto replacement = service.Prepare(Definition(2));
    Check(replacement.HasValue());
    Check(service.QueueActivation(std::move(replacement).Value()).HasValue());
    SceneCommandBuffer rejectedDuringTransition;
    [[maybe_unused]] const DeferredEntity rejected = rejectedDuringTransition.Create(RuntimeEntityCreateInfo{});
    Check(service.QueueStructuralCommands(std::move(rejectedDuringTransition)).HasError());
    Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(token)).HasValue());
    Check(service.ActiveScene()->RuntimeId() != preparedId);
    Check(service.ActiveScene()->Get(oldEntity).HasError());

    SceneCommandBuffer invalidStructuralBatch;
    invalidStructuralBatch.Destroy(oldEntity);
    Check(service.QueueStructuralCommands(std::move(invalidStructuralBatch)).HasValue());
    const SceneRuntimeId replacementId = service.ActiveScene()->RuntimeId();
    Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(token)).HasValue());
    Check(service.TakeOperationError().has_value());
    Check(service.ActiveScene()->RuntimeId() == replacementId);

    const std::size_t before = gAllocations.load(std::memory_order_relaxed);
    const RuntimeSceneView view = *service.ActiveScene();
    const auto found = view.Find(SceneObjectId{1});
    Check(found.has_value());
    Check(view.Get(*found).HasValue());
    Check(service.OnPhase(RuntimePhase::BeginFrame, Context(token)).HasValue());
    Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(token)).HasValue());
    const std::size_t after = gAllocations.load(std::memory_order_relaxed);
    Check(after == before);

    Check(service.QueueUnload().HasValue());
    Check(service.ActiveScene().has_value());
    Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(token)).HasValue());
    Check(!service.ActiveScene());
    Check(service.QueueUnload().HasValue());
    service.Shutdown();
}
} // namespace

int main()
{
    DefinitionValidation();
    IdentityAndTransactionalCommands();
    GenerationRetirement();
    LifecycleAndSteadyState();
    return 0;
}
