#pragma once

/**
 * @file RuntimeScene.h
 * @brief Generation-checked runtime scene ownership and deferred structural mutations.
 */

#include "Horo/Runtime/RuntimeLifecycle.h"
#include "Horo/Runtime/Scene/RuntimeSceneDefinition.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace Horo::Runtime
{
/** @brief Unique identity of one activated runtime-scene instance. */
struct SceneRuntimeId
{
    std::uint64_t value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }
    [[nodiscard]] constexpr auto operator<=>(const SceneRuntimeId &) const noexcept = default;
};

/** @brief Generation-checked entity slot identity within one runtime scene. */
struct EntityId
{
    std::uint32_t index{};
    std::uint32_t generation{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return generation != 0;
    }
    [[nodiscard]] constexpr auto operator<=>(const EntityId &) const noexcept = default;
};

/** @brief Complete runtime entity reference including its owning scene domain. */
struct EntityRef
{
    SceneRuntimeId runtime;
    EntityId entity;
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return runtime.IsValid() && entity.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const EntityRef &) const noexcept = default;
};

/** @brief Testable generation-retirement policy; production uses the uint32 maximum. */
struct RuntimeSceneConfig
{
    std::uint32_t maximumGeneration{std::numeric_limits<std::uint32_t>::max()};
};

/** @brief Complete initial topology and component state for a deferred runtime create. */
struct RuntimeEntityCreateInfo
{
    Math::Transform localTransform;
    std::optional<EntityRef> parent;
    std::optional<SceneObjectId> authoredObject;
    std::optional<PrimitiveMeshDescriptor> primitiveMesh;
    RuntimeComponentSet components;
};

/** @brief Stable token resolved to an EntityRef only after a successful structural commit. */
struct DeferredEntity
{
    std::uint64_t value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }
    [[nodiscard]] constexpr auto operator<=>(const DeferredEntity &) const noexcept = default;
};

/** @brief One deferred-create resolution emitted by a successful structural commit. */
struct DeferredEntityResolution
{
    DeferredEntity deferred;
    EntityRef entity;
};

/** @brief Atomic result of one committed structural command batch. */
struct StructuralCommitResult
{
    std::vector<DeferredEntityResolution> created;
    std::size_t destroyed{};
};

/** @brief Owner-thread command buffer for structural changes at the lifecycle safe point. */
class SceneCommandBuffer final
{
  public:
    /** @brief Queues an entity with its complete initial component topology. @param createInfo Complete initial state
     * copied into the command. @return Batch-local deferred token. */
    [[nodiscard]] DeferredEntity Create(RuntimeEntityCreateInfo createInfo);
    /** @brief Queues destruction of an existing generation-checked entity. @param entity Reference validated when the
     * batch commits. */
    void Destroy(EntityRef entity);
    /** @brief Reports whether no structural commands are queued. @return True when the buffer has no commands. */
    [[nodiscard]] bool Empty() const noexcept;

  private:
    friend class RuntimeScene;
    struct CreateCommand
    {
        DeferredEntity deferred;
        RuntimeEntityCreateInfo info;
    };
    struct DestroyCommand
    {
        EntityRef entity;
    };
    using Command = std::variant<CreateCommand, DestroyCommand>;
    std::vector<Command> commands_;
    std::uint64_t nextDeferred_{1};
};

/** @brief Allocation-free value view of one active runtime entity slot. */
struct RuntimeEntityView
{
    EntityRef entity;
    std::optional<SceneObjectId> authoredObject;
    std::optional<EntityRef> parent;
    const Math::Transform *localTransform{};
    const std::optional<PrimitiveMeshDescriptor> *primitiveMesh{};
    const RuntimeComponentSet *components{};
};

/** @brief Borrowed immutable view of one runtime scene; invalidated by structural commit or transition. */
class RuntimeSceneView final
{
  public:
    RuntimeSceneView() = default;
    /** @brief Returns the owning runtime identity. */
    [[nodiscard]] SceneRuntimeId RuntimeId() const noexcept;
    /** @brief Returns the logical definition identity. */
    [[nodiscard]] SceneDefinitionId DefinitionId() const noexcept;
    /** @brief Returns the activated authored revision. */
    [[nodiscard]] SceneDefinitionRevision DefinitionRevision() const noexcept;
    /** @brief Returns the number of allocated slots, including inactive and retired slots. */
    [[nodiscard]] std::size_t SlotCount() const noexcept;
    /** @brief Returns an active slot view or an empty value for inactive/out-of-range slots. @param slot Zero-based
     * slot index. @return Borrowed active entity view when present. */
    [[nodiscard]] std::optional<RuntimeEntityView> EntityAt(std::size_t slot) const noexcept;
    /** @brief Resolves a stable authored identity without allocation. @param object Non-zero authored identity. @return
     * Current runtime reference when mapped. */
    [[nodiscard]] std::optional<EntityRef> Find(SceneObjectId object) const noexcept;
    /** @brief Validates and returns one runtime entity view. @param entity Generation-checked reference to validate.
     * @return Borrowed entity view or a typed stale-reference error. */
    [[nodiscard]] Result<RuntimeEntityView> Get(EntityRef entity) const;

  private:
    friend class RuntimeScene;
    explicit RuntimeSceneView(const class RuntimeScene &scene) noexcept;
    const class RuntimeScene *scene_{};
};

/** @brief Owner of one instantiated scene registry and all typed component values. */
class RuntimeScene final
{
  public:
    /** @brief Instantiates a validated definition into a fresh runtime domain. @param definition Validated immutable
     * construction input. @param runtimeId Unique non-zero runtime identity. @param config Generation retirement
     * policy. @return Owned scene or a typed construction error. */
    [[nodiscard]] static Result<std::unique_ptr<RuntimeScene>> Create(const RuntimeSceneDefinition &definition,
                                                                      SceneRuntimeId runtimeId,
                                                                      RuntimeSceneConfig config = {});
    RuntimeScene(const RuntimeScene &) = default;
    RuntimeScene &operator=(const RuntimeScene &) = default;
    RuntimeScene(RuntimeScene &&) noexcept = default;
    RuntimeScene &operator=(RuntimeScene &&) noexcept = default;
    /** @brief Returns an immutable borrowed scene view. */
    [[nodiscard]] RuntimeSceneView View() const noexcept;
    /** @brief Applies a structural batch atomically, leaving the scene unchanged on failure. @param commands
     * Owner-thread command batch consumed by the operation. @return Created-token resolutions and destroy count, or the
     * first typed error. */
    [[nodiscard]] Result<StructuralCommitResult> Commit(SceneCommandBuffer commands);

  private:
    friend class RuntimeSceneView;
    struct Slot
    {
        std::uint32_t generation{1};
        bool active{};
        bool retired{};
        std::optional<SceneObjectId> authoredObject;
        std::optional<EntityId> parent;
        Math::Transform localTransform;
        std::optional<PrimitiveMeshDescriptor> primitiveMesh;
        RuntimeComponentSet components;
    };

    RuntimeScene(SceneRuntimeId runtimeId, SceneDefinitionId definitionId, SceneDefinitionRevision revision,
                 RuntimeSceneConfig config) noexcept;
    [[nodiscard]] Result<EntityRef> CreateEntity(const RuntimeEntityCreateInfo &info);
    [[nodiscard]] Result<void> DestroyEntity(EntityRef entity);
    [[nodiscard]] bool IsValid(EntityRef entity) const noexcept;

    SceneRuntimeId runtimeId_;
    SceneDefinitionId definitionId_;
    SceneDefinitionRevision definitionRevision_;
    RuntimeSceneConfig config_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeList_;
    std::vector<std::pair<SceneObjectId, EntityId>> authoredIndex_;
};

/** @brief Runtime lifecycle participant owning active, pending, and structural scene state. */
class RuntimeSceneService final : public RuntimeLifecycleParticipant
{
  public:
    RuntimeSceneService() = default;
    /** @brief Builds a candidate without changing the active scene. @param definition Validated immutable scene
     * definition. @param config Generation retirement policy. @return Owned candidate or a typed preparation error. */
    [[nodiscard]] Result<std::unique_ptr<RuntimeScene>> Prepare(const RuntimeSceneDefinition &definition,
                                                                RuntimeSceneConfig config = {});
    /** @brief Queues one prepared candidate for safe-point activation. @param candidate Non-null scene produced by
     * Prepare. @return Success or a typed pending-operation error. */
    [[nodiscard]] Result<void> QueueActivation(std::unique_ptr<RuntimeScene> candidate);
    /** @brief Queues active-scene unload; repeated unload with no pending transition is harmless. */
    [[nodiscard]] Result<void> QueueUnload();
    /** @brief Queues one structural batch against the current active scene. @param commands Batch consumed on success.
     * @return Success or a typed state/pending-operation error. */
    [[nodiscard]] Result<void> QueueStructuralCommands(SceneCommandBuffer commands);
    /** @brief Returns the current immutable active scene view. */
    [[nodiscard]] std::optional<RuntimeSceneView> ActiveScene() const noexcept;
    /** @brief Returns and clears the newest structural commit result. */
    [[nodiscard]] std::optional<StructuralCommitResult> TakeStructuralCommitResult();
    /** @brief Returns and clears the newest recoverable operation error. */
    [[nodiscard]] std::optional<Error> TakeOperationError();

    [[nodiscard]] Result<void> Startup(const CancellationToken &cancellation) override;
    [[nodiscard]] Result<void> OnPhase(RuntimePhase phase, const FrameContext &context) override;
    [[nodiscard]] Result<void> OnFixedUpdate(const FixedStepContext &context) override;
    void Shutdown() noexcept override;

  private:
    enum class TransitionKind : std::uint8_t
    {
        None,
        Activate,
        Unload
    };
    [[nodiscard]] Result<void> CommitDeferredChanges();

    std::unique_ptr<RuntimeScene> active_;
    std::unique_ptr<RuntimeScene> pending_;
    std::optional<SceneCommandBuffer> structuralCommands_;
    std::optional<StructuralCommitResult> structuralResult_;
    std::optional<Error> operationError_;
    std::uint64_t nextRuntimeId_{1};
    TransitionKind transition_{TransitionKind::None};
    bool started_{};
};
} // namespace Horo::Runtime
