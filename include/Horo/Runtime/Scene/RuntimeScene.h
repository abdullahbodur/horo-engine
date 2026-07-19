#pragma once

/**
 * @file RuntimeScene.h
 * @brief Generation-checked runtime scene ownership and deferred structural mutations.
 */

#include "Horo/Assets/AssetRegistry.h"
#include "Horo/Runtime/RuntimeLifecycle.h"
#include "Horo/Runtime/Scene/RuntimeSceneDefinition.h"

#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace Horo::Assets
{
    class AssetLoadService;
}

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

        [[nodiscard]] constexpr auto operator<=>(const SceneRuntimeId&) const noexcept = default;
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

        [[nodiscard]] constexpr auto operator<=>(const EntityId&) const noexcept = default;
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

        [[nodiscard]] constexpr auto operator<=>(const EntityRef&) const noexcept = default;
    };

    /** @brief Testable generation-retirement policy; production uses the uint32 maximum. */
    struct RuntimeSceneConfig
    {
        std::uint32_t maximumGeneration{std::numeric_limits<std::uint32_t>::max()};
    };

    /** @brief Bounded asset preparation limits for one runtime scene candidate. */
    struct RuntimeSceneAssetLimits
    {
        std::size_t maximumDependencies{1024}; /**< Maximum required assets in one definition. */
        std::size_t maximumConcurrentLoads{8}; /**< Maximum in-flight provider requests. */
        std::size_t maximumResidentBytes{1024ULL * 1024ULL * 1024ULL}; /**< Per-candidate logical byte budget. */
    };

    /** @brief Allocation-free borrowed cooked payload resolved for an active scene. */
    struct RuntimeSceneAssetView
    {
        Assets::AssetId id; /**< Stable identity of the resolved payload. */
        const Assets::AssetTypeId* type{}; /**< Borrowed validated type owned by the scene. */
        std::span<const std::uint8_t> bytes; /**< Borrowed immutable cooked bytes. */
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

        [[nodiscard]] constexpr auto operator<=>(const DeferredEntity&) const noexcept = default;
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
        const Math::Transform* localTransform{};
        const std::optional<PrimitiveMeshDescriptor>* primitiveMesh{};
        const RuntimeComponentSet* components{};
    };

    /** @brief Borrowed immutable view of one runtime scene; invalidated by structural commit or transition. */
    class RuntimeSceneView final
    {
    public:
        RuntimeSceneView() = default;
        /** @brief Reports whether this borrow still observes the structural revision captured at acquisition. */
        [[nodiscard]] bool IsCurrent() const noexcept;
        /** @brief Returns the owning runtime identity. */
        [[nodiscard]] SceneRuntimeId RuntimeId() const noexcept;
        /** @brief Returns the logical definition identity. */
        [[nodiscard]] SceneDefinitionId DefinitionId() const noexcept;
        /** @brief Returns the activated authored revision. */
        [[nodiscard]] SceneDefinitionRevision DefinitionRevision() const noexcept;
        /** @brief Returns the asset-registry revision used to prepare this scene. */
        [[nodiscard]] Assets::AssetRegistryRevision AssetRegistryRevision() const noexcept;
        /** @brief Returns the number of allocated slots, including inactive and retired slots. */
        [[nodiscard]] std::size_t SlotCount() const noexcept;
        /** @brief Returns an active slot view or an empty value for inactive/out-of-range slots. @param slot Zero-based
         * slot index. @return Borrowed active entity view when present. */
        [[nodiscard]] std::optional<RuntimeEntityView> EntityAt(std::size_t slot) const noexcept;
        /** @brief Resolves a stable authored identity without allocation. @param object Non-zero authored identity. @return
         * Current runtime reference when mapped. */
        [[nodiscard]] std::optional<EntityRef> Find(SceneObjectId object) const noexcept;
        /** @brief Resolves one scene-owned cooked payload without allocation. @param id Stable asset identity. @return
         * Borrowed payload while this view remains current, or empty when absent. */
        [[nodiscard]] std::optional<RuntimeSceneAssetView> FindAsset(Assets::AssetId id) const noexcept;
        /** @brief Validates and returns one runtime entity view. @param entity Generation-checked reference to validate.
         * @return Borrowed entity view or a typed stale-reference error. */
        [[nodiscard]] Result<RuntimeEntityView> Get(EntityRef entity) const;

    private:
        friend class RuntimeScene;
        explicit RuntimeSceneView(const class RuntimeScene& scene) noexcept;
        const class RuntimeScene* scene_{};
        std::uint64_t structuralRevision_{};
    };

    /** @brief Owner of one instantiated scene registry and all typed component values. */
    class RuntimeScene final
    {
    public:
        /** @brief Instantiates an assetless validated definition into a fresh runtime domain. Asset-bearing definitions
         * must use RuntimeSceneService::QueuePreparation. @param definition Validated immutable construction input.
         * @param runtimeId Unique non-zero runtime identity. @param config Generation retirement policy. @return Owned
         * scene or a typed construction error. */
        [[nodiscard]] static Result<std::unique_ptr<RuntimeScene>> Create(const RuntimeSceneDefinition& definition,
                                                                          SceneRuntimeId runtimeId,
                                                                          RuntimeSceneConfig config = {});
        RuntimeScene(const RuntimeScene&) = delete;
        RuntimeScene& operator=(const RuntimeScene&) = delete;
        RuntimeScene(RuntimeScene&&) = delete;
        RuntimeScene& operator=(RuntimeScene&&) = delete;
        /** @brief Returns an immutable borrowed scene view. */
        [[nodiscard]] RuntimeSceneView View() const noexcept;
        /** @brief Applies a structural batch atomically, leaving the scene unchanged on failure. @param commands
         * Owner-thread command batch consumed by the operation. @return Created-token resolutions and destroy count, or the
         * first typed error. */
        [[nodiscard]] Result<StructuralCommitResult> Commit(SceneCommandBuffer commands);

    private:
        friend class RuntimeSceneView;
        friend class RuntimeSceneService;

        struct ResolvedAsset
        {
            SceneAssetDependency dependency;
            std::shared_ptr<const std::vector<std::uint8_t>> payload;
        };

        [[nodiscard]] static Result<std::unique_ptr<RuntimeScene>> CreateResolved(
            const RuntimeSceneDefinition& definition, SceneRuntimeId runtimeId, RuntimeSceneConfig config,
            Assets::AssetRegistryRevision assetRevision, std::vector<ResolvedAsset> assets);

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

        /** @brief Copyable transactional state without a runtime-domain identity. */
        struct RuntimeSceneStorage
        {
            std::vector<Slot> slots;
            std::vector<std::uint32_t> freeList;
            std::vector<std::pair<SceneObjectId, EntityId>> authoredIndex;
        };

        RuntimeScene(SceneRuntimeId runtimeId, SceneDefinitionId definitionId, SceneDefinitionRevision revision,
                     RuntimeSceneConfig config, Assets::AssetRegistryRevision assetRevision,
                     std::vector<ResolvedAsset> assets) noexcept;
        [[nodiscard]] Result<EntityRef> CreateEntity(RuntimeSceneStorage& storage,
                                                     const RuntimeEntityCreateInfo& info) const;
        [[nodiscard]] Result<void> DestroyEntity(RuntimeSceneStorage& storage, EntityRef entity) const;
        [[nodiscard]] bool IsValid(const RuntimeSceneStorage& storage, EntityRef entity) const noexcept;

        SceneRuntimeId runtimeId_;
        SceneDefinitionId definitionId_;
        SceneDefinitionRevision definitionRevision_;
        Assets::AssetRegistryRevision assetRegistryRevision_;
        std::vector<ResolvedAsset> assets_;
        RuntimeSceneConfig config_;
        RuntimeSceneStorage storage_;
        std::uint64_t structuralRevision_{1};
    };

    /** @brief Runtime lifecycle participant owning active, pending, and structural scene state. */
    class RuntimeSceneService final : public RuntimeLifecycleParticipant
    {
    public:
        /** @brief Creates an assetless scene service for editor previews and headless typed-scene tests. */
        RuntimeSceneService();
        /** @brief Creates a scene service with borrowed asset services. @param registry Registry that outlives this
         * service. @param loads Load service that outlives this service. @param limits Per-candidate preparation limits. */
        RuntimeSceneService(Assets::AssetRegistry& registry, Assets::AssetLoadService& loads,
                            RuntimeSceneAssetLimits limits = {});
        ~RuntimeSceneService() override;
        /** @brief Queues validated preparation and later safe-point activation. Asset-bearing definitions pin the current
         * registry snapshot and load through the injected service. @param definition Immutable scene definition copied by
         * the operation. @param config Generation retirement policy. @return Success when accepted, or a typed immediate
         * validation/admission error. */
        [[nodiscard]] Result<void> QueuePreparation(const RuntimeSceneDefinition& definition,
                                                    RuntimeSceneConfig config = {});
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

        [[nodiscard]] Result<void> Startup(const CancellationToken& cancellation) override;
        [[nodiscard]] Result<void> OnPhase(RuntimePhase phase, const FrameContext& context) override;
        [[nodiscard]] Result<void> OnFixedUpdate(const FixedStepContext& context) override;
        void Shutdown() noexcept override;

    private:
        enum class TransitionKind : std::uint8_t
        {
            None,
            Activate,
            Unload
        };

        struct Preparation;
        [[nodiscard]] Result<void>
        BeginPreparation(const RuntimeSceneDefinition& definition, RuntimeSceneConfig config);
        void AdvancePreparation();
        void CancelPreparation(bool waitForCompletion) noexcept;
        [[nodiscard]] Result<void> SubmitPreparationLoads();
        [[nodiscard]] Result<void> CommitDeferredChanges();

        std::unique_ptr<RuntimeScene> active_;
        std::unique_ptr<RuntimeScene> pending_;
        std::unique_ptr<Preparation> preparation_;
        std::optional<SceneCommandBuffer> structuralCommands_;
        std::optional<StructuralCommitResult> structuralResult_;
        std::optional<Error> operationError_;
        std::uint64_t nextRuntimeId_{1};
        Assets::AssetRegistry* assetRegistry_{};
        Assets::AssetLoadService* assetLoads_{};
        RuntimeSceneAssetLimits assetLimits_{};
        TransitionKind transition_{TransitionKind::None};
        bool started_{};
        bool shutdown_{};
    };
} // namespace Horo::Runtime
