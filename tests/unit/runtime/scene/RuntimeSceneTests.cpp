#include <catch2/catch_test_macros.hpp>

#include "Horo/Assets/AssetProvider.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <type_traits>

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
using namespace Horo::Assets;
using namespace Horo::Runtime;

static_assert(!std::is_copy_constructible_v<RuntimeScene>);
static_assert(!std::is_copy_assignable_v<RuntimeScene>);
static_assert(!std::is_move_constructible_v<RuntimeScene>);
static_assert(!std::is_move_assignable_v<RuntimeScene>);

void Check(bool condition)
{
    REQUIRE((condition));
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

AssetId Asset(const std::string_view value)
{
    auto parsed = AssetId::Parse(value);
    Check(parsed.HasValue());
    return parsed.Value();
}

AssetTypeId AssetType(const std::string_view value = "core.mesh")
{
    auto parsed = AssetTypeId::Parse(value);
    Check(parsed.HasValue());
    return parsed.Value();
}

ProjectPath Path(const std::string_view value)
{
    auto parsed = ProjectPath::Parse(value);
    Check(parsed.HasValue());
    return parsed.Value();
}

AssetRecord Record(const AssetId id, const AssetTypeId &type = AssetType())
{
    const std::string name = id.ToString();
    return {id, type, Path("assets/" + name + ".bin"), Path("assets/" + name + ".bin.horo")};
}

RuntimeSceneDefinition AssetDefinition(const std::span<const SceneAssetDependency> dependencies,
                                       const std::uint64_t revision = 1)
{
    SceneDefinitionBuilder builder{SceneDefinitionId{9}, SceneDefinitionRevision{revision}};
    builder.Add(Entity(1));
    for (const SceneAssetDependency &dependency : dependencies)
        Check(builder.RequireAsset(dependency).HasValue());
    auto built = std::move(builder).Build();
    Check(built.HasValue());
    return std::move(built).Value();
}

FrameContext Context(const CancellationToken &token);

std::optional<Error> Pump(RuntimeSceneService &service, const CancellationToken &token,
                          const SceneDefinitionRevision expectedRevision = SceneDefinitionRevision{1},
                          const std::size_t maximumIterations = 2000)
{
    for (std::size_t iteration = 0; iteration < maximumIterations; ++iteration)
    {
        Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(token)).HasValue());
        if (std::optional<Error> error = service.TakeOperationError())
            return error;
        if (service.ActiveScene() && service.ActiveScene()->DefinitionRevision() == expectedRevision)
            return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    Check(false);
    return std::nullopt;
}

class TrackingProvider final : public IAssetProvider
{
  public:
    struct Payload
    {
        AssetId id;
        std::vector<std::uint8_t> bytes;
        mutable std::atomic<std::size_t> calls{};
    };

    void Add(const AssetId id, std::vector<std::uint8_t> bytes)
    {
        auto payload = std::make_unique<Payload>();
        payload->id = id;
        payload->bytes = std::move(bytes);
        payloads.push_back(std::move(payload));
    }

    Result<bool> Exists(const AssetId id, const CancellationToken &cancellation) const override
    {
        return cancellation.IsCancellationRequested() ? Result<bool>::Failure(Error{ErrorCode{"asset.load.cancelled"},
                                                                                    ErrorDomainId{"horo.asset"},
                                                                                    ErrorSeverity::Error,
                                                                                    "cancelled",
                                                                                    {}})
                                                      : Result<bool>::Success(Find(id) != nullptr);
    }

    Result<std::vector<std::uint8_t>> Load(const AssetId id, const CancellationToken &cancellation) const override
    {
        Payload *payload = Find(id);
        if (!payload)
            return Result<std::vector<std::uint8_t>>::Failure(Error{ErrorCode{"asset.provider.not_found"},
                                                                    ErrorDomainId{"horo.asset"},
                                                                    ErrorSeverity::Error,
                                                                    "missing",
                                                                    {}});
        payload->calls.fetch_add(1, std::memory_order_relaxed);
        const std::size_t now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
        std::size_t observed = maximumActive.load(std::memory_order_relaxed);
        while (observed < now && !maximumActive.compare_exchange_weak(observed, now, std::memory_order_relaxed))
        {
        }
        started.fetch_add(1, std::memory_order_release);
        while (blocked.load(std::memory_order_acquire) && !release.load(std::memory_order_acquire) &&
               !cancellation.IsCancellationRequested())
            std::this_thread::yield();
        active.fetch_sub(1, std::memory_order_acq_rel);
        if (cancellation.IsCancellationRequested())
            return Result<std::vector<std::uint8_t>>::Failure(Error{
                ErrorCode{"asset.load.cancelled"}, ErrorDomainId{"horo.asset"}, ErrorSeverity::Error, "cancelled", {}});
        return Result<std::vector<std::uint8_t>>::Success(payload->bytes);
    }

    [[nodiscard]] std::size_t Calls(const AssetId id) const
    {
        Payload *payload = Find(id);
        return payload ? payload->calls.load(std::memory_order_relaxed) : 0;
    }

    mutable std::atomic<std::size_t> active{};
    mutable std::atomic<std::size_t> maximumActive{};
    mutable std::atomic<std::size_t> started{};
    mutable std::atomic<bool> blocked{};
    mutable std::atomic<bool> release{};

  private:
    [[nodiscard]] Payload *Find(const AssetId id) const
    {
        const auto found = std::ranges::find(payloads, id, [](const auto &payload) { return payload->id; });
        return found == payloads.end() ? nullptr : found->get();
    }
    std::vector<std::unique_ptr<Payload>> payloads;
};

FrameContext Context(const CancellationToken &token)
{
    return FrameContext{1, {}, 0.0, 0, {}, false, token};
}

TEST_CASE("Definition Validation", "[unit][runtime][scene]")
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

TEST_CASE("Asset Definition Contract", "[unit][runtime][scene]")
{
    const AssetId first = Asset("00112233-4455-6677-8899-aabbccddeeff");
    const AssetId second = Asset("11112233-4455-6677-8899-aabbccddeeff");
    SceneDefinitionBuilder builder{SceneDefinitionId{1}, {}};
    builder.Add(Entity(1));
    Check(builder.RequireAsset({second, AssetType()}).HasValue());
    Check(builder.RequireAsset({first, AssetType()}).HasValue());
    Check(builder.RequireAsset({second, AssetType()}).HasValue());
    Check(builder.RequireAsset({second, AssetType("core.texture")}).ErrorValue().code.Value() ==
          "scene.asset.dependency_conflict");
    Check(builder.RequireAsset({{}, AssetType()}).ErrorValue().code.Value() == "scene.asset.dependency_invalid");
    auto definition = std::move(builder).Build();
    Check(definition.HasValue());
    Check(definition.Value().AssetDependencies().size() == 2);
    Check(definition.Value().AssetDependencies()[0].id == first);
    Check(definition.Value().AssetDependencies()[1].id == second);
    Check(RuntimeScene::Create(definition.Value(), SceneRuntimeId{1}).ErrorValue().code.Value() ==
          "scene.asset.services_unavailable");
}

TEST_CASE("Asset Preparation Validation And Lookup", "[unit][runtime][scene]")
{
    const AssetId first = Asset("00112233-4455-6677-8899-aabbccddeeff");
    const AssetId second = Asset("11112233-4455-6677-8899-aabbccddeeff");
    const std::array dependencies{SceneAssetDependency{first, AssetType()}, SceneAssetDependency{second, AssetType()}};
    const RuntimeSceneDefinition definition = AssetDefinition(dependencies);
    CancellationSource cancellation;

    RuntimeSceneService assetless;
    Check(assetless.Startup(cancellation.Token()).HasValue());
    Check(assetless.QueuePreparation(definition).ErrorValue().code.Value() == "scene.asset.services_unavailable");
    assetless.Shutdown();

    AssetRegistry registry;
    Check(registry.Publish({Record(first), Record(second)}).status == AssetRegistryBuildStatus::Complete);
    MemoryAssetProvider provider;
    provider.Insert(first, {1, 2, 3});
    provider.Insert(second, {4, 5});
    JobSystem jobs{JobSystemConfig{2, 16}};
    AssetLoadService loads{jobs, provider};
    RuntimeSceneService service{registry, loads};
    Check(service.Startup(cancellation.Token()).HasValue());
    Check(service.QueuePreparation(definition).HasValue());
    Check(!Pump(service, cancellation.Token()).has_value());
    const RuntimeSceneView view = *service.ActiveScene();
    Check(view.AssetRegistryRevision() == registry.Snapshot().Revision());
    const auto firstView = view.FindAsset(first);
    Check(firstView.has_value());
    Check(firstView->type && firstView->type->Value() == "core.mesh");
    Check(firstView->bytes.size() == 3 && firstView->bytes[2] == 3);
    const std::size_t before = gAllocations.load(std::memory_order_relaxed);
    for (int iteration = 0; iteration < 1000; ++iteration)
        Check(view.FindAsset(first).has_value() && !view.FindAsset(AssetId{}).has_value());
    Check(gAllocations.load(std::memory_order_relaxed) == before);
    service.Shutdown();
    loads.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Missing Type Empty And Invalid Limits Are Typed", "[unit][runtime][scene]")
{
    const AssetId first = Asset("00112233-4455-6677-8899-aabbccddeeff");
    const AssetId missing = Asset("11112233-4455-6677-8899-aabbccddeeff");
    CancellationSource cancellation;
    AssetRegistry registry;
    Check(registry.Publish({Record(first, AssetType("core.texture"))}).status == AssetRegistryBuildStatus::Complete);
    MemoryAssetProvider provider;
    provider.Insert(first, {});
    JobSystem jobs{JobSystemConfig{1, 8}};
    AssetLoadService loads{jobs, provider};
    RuntimeSceneService service{registry, loads};
    Check(service.Startup(cancellation.Token()).HasValue());
    const std::array wrongType{SceneAssetDependency{first, AssetType()}};
    Check(service.QueuePreparation(AssetDefinition(wrongType)).ErrorValue().code.Value() ==
          "scene.asset.type_mismatch");
    const std::array absent{SceneAssetDependency{missing, AssetType()}};
    Check(service.QueuePreparation(AssetDefinition(absent)).ErrorValue().code.Value() == "scene.asset.missing");

    Check(registry.Publish({Record(first)}).status == AssetRegistryBuildStatus::Complete);
    Check(service.QueuePreparation(AssetDefinition(wrongType)).HasValue());
    const std::optional<Error> emptyError = Pump(service, cancellation.Token());
    Check(emptyError && emptyError->code.Value() == "scene.asset.payload_empty");

    Check(registry.Publish({Record(first), Record(missing)}).status == AssetRegistryBuildStatus::Complete);
    Check(service.QueuePreparation(AssetDefinition(absent, 2)).HasValue());
    const std::optional<Error> providerError = Pump(service, cancellation.Token(), SceneDefinitionRevision{2});
    Check(providerError && providerError->code.Value() == "asset.provider.not_found");

    RuntimeSceneService invalidLimits{registry, loads, RuntimeSceneAssetLimits{1, 0, 32}};
    Check(invalidLimits.Startup(cancellation.Token()).HasValue());
    Check(invalidLimits.QueuePreparation(AssetDefinition(wrongType)).ErrorValue().code.Value() ==
          "scene.asset.limits_invalid");
    invalidLimits.Shutdown();
    service.Shutdown();
    loads.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Partial Submission Queue Pressure Is Typed", "[unit][runtime][scene]")
{
    const AssetId first = Asset("00112233-4455-6677-8899-aabbccddeeff");
    const AssetId second = Asset("11112233-4455-6677-8899-aabbccddeeff");
    AssetRegistry registry;
    Check(registry.Publish({Record(first), Record(second)}).status == AssetRegistryBuildStatus::Complete);
    TrackingProvider provider;
    provider.Add(first, {1});
    provider.Add(second, {2});
    provider.blocked.store(true, std::memory_order_release);
    JobSystem jobs{JobSystemConfig{1, 8}};
    AssetLoadService loads{jobs, provider, 1};
    RuntimeSceneService service{registry, loads, RuntimeSceneAssetLimits{8, 2, 64}};
    CancellationSource cancellation;
    Check(service.Startup(cancellation.Token()).HasValue());
    const std::array dependencies{SceneAssetDependency{first, AssetType()}, SceneAssetDependency{second, AssetType()}};
    const Result<void> queued = service.QueuePreparation(AssetDefinition(dependencies));
    Check(queued.HasError() && queued.ErrorValue().code.Value() == "asset.load.queue_full");
    Check(!service.ActiveScene());
    service.Shutdown();
    loads.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Budget Is Incremental And Payloads Are Reused", "[unit][runtime][scene]")
{
    const AssetId first = Asset("00112233-4455-6677-8899-aabbccddeeff");
    const AssetId second = Asset("11112233-4455-6677-8899-aabbccddeeff");
    const AssetId third = Asset("21112233-4455-6677-8899-aabbccddeeff");
    AssetRegistry registry;
    Check(registry.Publish({Record(first), Record(second), Record(third)}).status ==
          AssetRegistryBuildStatus::Complete);
    TrackingProvider provider;
    provider.Add(first, {1, 2, 3, 4});
    provider.Add(second, {5, 6, 7, 8});
    provider.Add(third, {9, 10, 11, 12});
    JobSystem jobs{JobSystemConfig{3, 16}};
    AssetLoadService loads{jobs, provider};
    CancellationSource cancellation;
    RuntimeSceneService service{registry, loads, RuntimeSceneAssetLimits{16, 1, 5}};
    Check(service.Startup(cancellation.Token()).HasValue());
    const std::array all{SceneAssetDependency{first, AssetType()}, SceneAssetDependency{second, AssetType()},
                         SceneAssetDependency{third, AssetType()}};
    Check(service.QueuePreparation(AssetDefinition(all)).HasValue());
    const std::optional<Error> budgetError = Pump(service, cancellation.Token());
    Check(budgetError && budgetError->code.Value() == "scene.asset.budget_exceeded");
    Check(provider.Calls(first) == 1 && provider.Calls(second) == 1 && provider.Calls(third) == 0);
    service.Shutdown();

    RuntimeSceneService reuse{registry, loads, RuntimeSceneAssetLimits{16, 2, 64}};
    Check(reuse.Startup(cancellation.Token()).HasValue());
    const std::array firstSet{SceneAssetDependency{first, AssetType()}, SceneAssetDependency{second, AssetType()}};
    Check(reuse.QueuePreparation(AssetDefinition(firstSet, 1)).HasValue());
    Check(!Pump(reuse, cancellation.Token(), SceneDefinitionRevision{1}).has_value());
    const std::size_t secondCalls = provider.Calls(second);
    const std::array replacement{SceneAssetDependency{second, AssetType()}, SceneAssetDependency{third, AssetType()}};
    Check(reuse.QueuePreparation(AssetDefinition(replacement, 2)).HasValue());
    Check(!Pump(reuse, cancellation.Token(), SceneDefinitionRevision{2}).has_value());
    Check(provider.Calls(second) == secondCalls);
    Check(provider.Calls(third) == 1);
    reuse.Shutdown();
    loads.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Concurrency Is Bounded At Eight", "[unit][runtime][scene]")
{
    constexpr std::array ids{"00112233-4455-6677-8899-aabbccddeeff", "10112233-4455-6677-8899-aabbccddeeff",
                             "20112233-4455-6677-8899-aabbccddeeff", "30112233-4455-6677-8899-aabbccddeeff",
                             "40112233-4455-6677-8899-aabbccddeeff", "50112233-4455-6677-8899-aabbccddeeff",
                             "60112233-4455-6677-8899-aabbccddeeff", "70112233-4455-6677-8899-aabbccddeeff",
                             "80112233-4455-6677-8899-aabbccddeeff"};
    std::vector<AssetRecord> records;
    std::vector<SceneAssetDependency> dependencies;
    TrackingProvider provider;
    for (const std::string_view text : ids)
    {
        const AssetId id = Asset(text);
        records.push_back(Record(id));
        dependencies.push_back({id, AssetType()});
        provider.Add(id, {1});
    }
    AssetRegistry registry;
    Check(registry.Publish(std::move(records)).status == AssetRegistryBuildStatus::Complete);
    provider.blocked.store(true, std::memory_order_release);
    JobSystem jobs{JobSystemConfig{9, 32}};
    AssetLoadService loads{jobs, provider};
    RuntimeSceneService service{registry, loads, RuntimeSceneAssetLimits{16, 8, 64}};
    CancellationSource cancellation;
    Check(service.Startup(cancellation.Token()).HasValue());
    Check(service.QueuePreparation(AssetDefinition(dependencies)).HasValue());
    for (std::size_t spin = 0; spin < 100000 && provider.started.load(std::memory_order_acquire) < 8; ++spin)
        std::this_thread::yield();
    Check(provider.started.load(std::memory_order_acquire) == 8);
    Check(provider.maximumActive.load(std::memory_order_acquire) == 8);
    provider.release.store(true, std::memory_order_release);
    Check(!Pump(service, cancellation.Token()).has_value());
    Check(provider.started.load(std::memory_order_acquire) == 9);
    Check(provider.maximumActive.load(std::memory_order_acquire) <= 8);
    service.Shutdown();
    loads.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Registry Replacement Rejects Stale Candidate And Preserves Active", "[unit][runtime][scene]")
{
    const AssetId first = Asset("00112233-4455-6677-8899-aabbccddeeff");
    const AssetId second = Asset("11112233-4455-6677-8899-aabbccddeeff");
    AssetRegistry registry;
    Check(registry.Publish({Record(first), Record(second)}).status == AssetRegistryBuildStatus::Complete);
    TrackingProvider provider;
    provider.Add(first, {1});
    provider.Add(second, {2});
    JobSystem jobs{JobSystemConfig{2, 16}};
    AssetLoadService loads{jobs, provider};
    RuntimeSceneService service{registry, loads};
    CancellationSource cancellation;
    Check(service.Startup(cancellation.Token()).HasValue());
    const std::array initial{SceneAssetDependency{first, AssetType()}};
    Check(service.QueuePreparation(AssetDefinition(initial, 1)).HasValue());
    Check(!Pump(service, cancellation.Token(), SceneDefinitionRevision{1}).has_value());
    const SceneRuntimeId activeId = service.ActiveScene()->RuntimeId();

    provider.blocked.store(true, std::memory_order_release);
    provider.release.store(false, std::memory_order_release);
    provider.started.store(0, std::memory_order_release);
    const std::array replacement{SceneAssetDependency{second, AssetType()}};
    Check(service.QueuePreparation(AssetDefinition(replacement, 2)).HasValue());
    for (std::size_t spin = 0; spin < 100000 && provider.started.load(std::memory_order_acquire) == 0; ++spin)
        std::this_thread::yield();
    Check(provider.started.load(std::memory_order_acquire) == 1);
    Check(registry.Publish({Record(first), Record(second)}).status == AssetRegistryBuildStatus::Complete);
    provider.release.store(true, std::memory_order_release);
    const std::optional<Error> stale = Pump(service, cancellation.Token(), SceneDefinitionRevision{2});
    Check(stale && stale->code.Value() == "scene.asset.registry_stale");
    Check(service.ActiveScene()->RuntimeId() == activeId);
    Check(service.ActiveScene()->DefinitionRevision() == SceneDefinitionRevision{1});
    service.Shutdown();
    loads.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Unload And Shutdown Cancel Preparation", "[unit][runtime][scene]")
{
    const AssetId id = Asset("00112233-4455-6677-8899-aabbccddeeff");
    AssetRegistry registry;
    Check(registry.Publish({Record(id)}).status == AssetRegistryBuildStatus::Complete);
    TrackingProvider provider;
    provider.Add(id, {1});
    provider.blocked.store(true, std::memory_order_release);
    JobSystem jobs{JobSystemConfig{1, 8}};
    AssetLoadService loads{jobs, provider};
    RuntimeSceneService service{registry, loads};
    CancellationSource cancellation;
    Check(service.Startup(cancellation.Token()).HasValue());
    const std::array dependencies{SceneAssetDependency{id, AssetType()}};
    Check(service.QueuePreparation(AssetDefinition(dependencies)).HasValue());
    for (std::size_t spin = 0; spin < 100000 && provider.started.load(std::memory_order_acquire) == 0; ++spin)
        std::this_thread::yield();
    Check(service.QueueUnload().HasValue());
    Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(cancellation.Token())).HasValue());
    Check(!service.ActiveScene());

    provider.started.store(0, std::memory_order_release);
    provider.release.store(false, std::memory_order_release);
    bool accepted{};
    for (std::size_t attempt = 0; attempt < 1000 && !accepted; ++attempt)
    {
        const Result<void> queued = service.QueuePreparation(AssetDefinition(dependencies, 2));
        accepted = queued.HasValue();
        if (!accepted)
        {
            Check(queued.ErrorValue().code.Value() == "asset.load.queue_full");
            std::this_thread::yield();
        }
    }
    Check(accepted);
    for (std::size_t spin = 0; spin < 100000 && provider.started.load(std::memory_order_acquire) == 0; ++spin)
        std::this_thread::yield();
    service.Shutdown();
    Check(service.QueuePreparation(AssetDefinition(dependencies, 3)).ErrorValue().code.Value() ==
          "scene.service.shutdown");
    service.Shutdown();
    loads.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Identity And Transactional Commands", "[unit][runtime][scene]")
{
    auto created = RuntimeScene::Create(Definition(), SceneRuntimeId{10});
    Check(created.HasValue());
    std::unique_ptr<RuntimeScene> scene = std::move(created).Value();
    const RuntimeSceneView initial = scene->View();
    const EntityRef root = *initial.Find(SceneObjectId{1});
    Check(initial.Get(root).HasValue());
    Check(initial.Get(EntityRef{SceneRuntimeId{11}, root.entity}).HasError());

    Check(scene->Commit({}).HasValue());
    Check(initial.IsCurrent());

    SceneCommandBuffer invalidBatch;
    [[maybe_unused]] const DeferredEntity invalidCreate = invalidBatch.Create(RuntimeEntityCreateInfo{});
    invalidBatch.Destroy(EntityRef{SceneRuntimeId{10}, EntityId{99, 1}});
    Check(scene->Commit(std::move(invalidBatch)).HasError());
    Check(initial.IsCurrent());
    Check(scene->View().SlotCount() == 2);

    SceneCommandBuffer destroyChild;
    destroyChild.Destroy(*scene->View().Find(SceneObjectId{2}));
    auto childDestroyed = scene->Commit(std::move(destroyChild));
    Check(childDestroyed.HasValue());
    Check(!initial.IsCurrent());
    Check(initial.Get(root).HasError());
    Check(initial.Get(root).ErrorValue().code.Value() == "scene.view.stale");
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

TEST_CASE("Generation Retirement", "[unit][runtime][scene]")
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

TEST_CASE("Lifecycle And Steady State", "[unit][runtime][scene]")
{
    RuntimeSceneService service;
    CancellationSource cancellation;
    const CancellationToken token = cancellation.Token();
    Check(service.Startup(token).HasValue());
    Check(service.QueuePreparation(Definition()).HasValue());
    Check(!service.ActiveScene());
    Check(service.OnPhase(RuntimePhase::CommitDeferredLifecycleChanges, Context(token)).HasValue());
    const SceneRuntimeId preparedId = service.ActiveScene()->RuntimeId();
    const EntityRef oldEntity = *service.ActiveScene()->Find(SceneObjectId{1});

    Check(service.QueuePreparation(Definition(2)).HasValue());
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
