#pragma once

/**
 * @file RenderFrontend.h
 * @brief Host-facing owner of one selected and initialized renderer backend.
 */

#include "Horo/Runtime/Render/RenderBackendRegistry.h"

#include <memory>
#include <span>
#include <vector>

namespace Horo::Render {
    namespace Detail {
        class RenderResourceRegistry;
        class RenderResourceUploadQueue;
    }  // namespace Detail

    /** @brief Finite frontend admission and per-drain limits for initial resource uploads. */
    struct RenderResourceUploadLimits {
        std::size_t maximumPendingBytes{64U * 1024U * 1024U};
        std::size_t maximumBytesPerDrain{16U * 1024U * 1024U};
        std::uint32_t maximumRequestsPerDrain{64};

        /** @brief Reports whether every upload bound is finite and non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return maximumPendingBytes > 0 && maximumBytesPerDrain > 0 && maximumBytesPerDrain <= maximumPendingBytes &&
                   maximumRequestsPerDrain > 0;
        }
    };

    class RenderFrontend;

    /**
     * @brief Move-only owner of one begun backend frame until presentation or abort.
     *
     * Destruction aborts an unpresented frame with its matching token. Destroying
     * the creating frontend first aborts and makes the outstanding scope inert.
     * Execute may succeed exactly once;
     * Present may succeed only after Execute. Invalid stage calls return typed errors
     * without consuming the scope so the caller may recover. Backend failures and
     * exceptions abort the scope before returning.
     *
     * All methods, moves, and destruction must run serially on the same host-declared
     * render-capable thread as the creating frontend. The scope is not thread-safe and
     * must not be transferred across threads or accessed concurrently.
     */
    class RenderFrameScope final {
    public:
        /** @brief Aborts the matching frame when this scope still owns one. */
        ~RenderFrameScope();

        RenderFrameScope(const RenderFrameScope &) = delete;
        RenderFrameScope &operator=(const RenderFrameScope &) = delete;

        /** @brief Transfers matching-frame ownership and leaves source inert. */
        RenderFrameScope(RenderFrameScope &&other) noexcept;

        /** @brief Aborts any currently owned frame, then transfers ownership. */
        RenderFrameScope &operator=(RenderFrameScope &&other) noexcept;

        /**
         * @brief Executes the ordered pass sequence for this frame exactly once.
         * @param orderedPasses Non-owning pass sequence valid for this call.
         * @return Success, a typed invalid-stage error, the original backend failure,
         * or a translated backend exception.
         */
        [[nodiscard]] Result<void> Execute(std::span<const RenderPassDescriptor> orderedPasses);

        /**
         * @brief Presents and consumes a successfully executed frame.
         * @return Success, a typed invalid-stage error, the original backend failure,
         * or a translated backend exception.
         */
        [[nodiscard]] Result<void> Present();

        /** @brief Explicitly aborts the owned frame; safe to call repeatedly. */
        void Cancel() noexcept;

    private:
        friend class RenderFrontend;

        RenderFrameScope(RenderFrontend &owner, IRenderBackend &backend, FrameToken frame) noexcept;
        void Abort() noexcept;
        void Release() noexcept;

        RenderFrontend *owner_{nullptr};
        IRenderBackend *backend_{nullptr};
        FrameToken frame_{};
        bool executed_{false};
    };

    /**
     * @brief Owns the initialized renderer backend for one host lifetime.
     *
     * Construction performs explicit registry selection and backend initialization.
     * Destruction deterministically shuts the backend down before releasing it.
     * All methods and destruction must run serially on one host-declared render-capable
     * thread. The frontend and its frame scope are not thread-safe.
     */
    class RenderFrontend final {
    public:
        /**
         * @brief Creates and initializes the selected backend from a sealed registry.
         * @param registry Host-owned sealed backend registry.
         * @param backendId Canonical backend identity selected by host policy.
         * @param config Backend-neutral initialization policy.
         * @param uploadLimits Finite initial-upload queue and per-safe-point work limits.
         * @return Owned frontend, or the backend creation/initialization failure.
         */
        [[nodiscard]] static Result<std::unique_ptr<RenderFrontend>> Create(const RenderBackendRegistry &registry,
                                                                            const RenderBackendId &backendId,
                                                                            const RenderBackendConfig &config,
                                                                            RenderResourceUploadLimits uploadLimits = {});

        /** @brief Shuts down and releases the owned backend. */
        ~RenderFrontend();

        RenderFrontend(const RenderFrontend &) = delete;
        RenderFrontend &operator=(const RenderFrontend &) = delete;
        RenderFrontend(RenderFrontend &&) = delete;
        RenderFrontend &operator=(RenderFrontend &&) = delete;

        /**
         * @brief Returns the immutable capability snapshot of the initialized backend.
         * @return Reference valid until frontend destruction; safe to query during an active frame.
         */
        [[nodiscard]] const RenderBackendCapabilities &Capabilities() const noexcept;

        /**
         * @brief Begins one staged frame owned by a move-only recovery scope.
         * @param descriptor Host frame identity and output extent.
         * @return Frame scope, the original typed backend failure, or a translated
         * backend exception. Destroying the frontend first safely invalidates the scope.
         */
        [[nodiscard]] Result<RenderFrameScope> BeginFrame(const FrameDescriptor &descriptor);

        /**
         * @brief Executes and presents one ordered frame, aborting backend frame state on failure.
         * @param descriptor Host frame identity and output extent.
         * @param orderedPasses Non-owning pass sequence valid for this call.
         * @return Success, the original typed backend failure, or a translated backend exception.
         */
        [[nodiscard]] Result<void> SubmitFrame(const FrameDescriptor &descriptor, std::span<const RenderPassDescriptor> orderedPasses);

        /**
         * @brief Commits a framebuffer resize through the owned backend.
         * @param extent Non-zero framebuffer extent committed by the host.
         * @return Backend result, a typed active-frame rejection, or a translated backend exception.
         */
        [[nodiscard]] Result<void> Resize(FramebufferExtent extent);

        /**
         * @brief Attaches the synchronously borrowed static-mesh executor used by frame execution.
         * @param executor Executor that must outlive its attachment or be detached before destruction.
         * @return Success or a typed rejection when an executor is already attached or a frame is active.
         */
        [[nodiscard]] Result<void> AttachStaticMeshPassExecutor(IStaticMeshPassExecutor &executor);

        /** @brief Detaches the matching executor; safe to call repeatedly outside an active frame. */
        void DetachStaticMeshPassExecutor(const IStaticMeshPassExecutor &executor) noexcept;

        /** @brief Creates one logical offscreen target identity with an initial non-zero extent. */
        [[nodiscard]] Result<RenderTargetHandle> CreateOffscreenTarget(FramebufferExtent extent);

        /** @brief Updates the extent associated with a live generation-safe target handle. */
        [[nodiscard]] Result<void> ResizeOffscreenTarget(RenderTargetHandle target, FramebufferExtent extent);

        /** @brief Releases a live target and invalidates its generation; repeated stale release is rejected. */
        [[nodiscard]] Result<void> ReleaseOffscreenTarget(RenderTargetHandle target);

        /**
         * @brief Queues an owned initial upload for one immutable buffer generation.
         * @param descriptor Valid backend-neutral buffer descriptor.
         * @param initialData Bytes copied into the bounded frontend queue before return.
         * @return Pending typed handle and completion operation, or an admission failure.
         */
        [[nodiscard]] Result<ResourceCreation<RenderBufferHandle>> CreateBuffer(const RenderBufferDescriptor &descriptor,
                                                                                std::span<const std::byte> initialData);

        /**
         * @brief Queues one immutable mesh over exact ready vertex and index buffers.
         * @param descriptor Valid mesh descriptor whose dependencies belong to this frontend.
         * @return Pending typed handle and completion operation, or a validation/admission failure.
         */
        [[nodiscard]] Result<ResourceCreation<RenderMeshHandle>> CreateMesh(const RenderMeshDescriptor &descriptor);

        /**
         * @brief Queues a new mesh generation and retires the old generation only after publication.
         * @param current Ready mesh generation to replace without retargeting its dependents.
         * @param descriptor Descriptor for the independent replacement generation.
         * @return Pending replacement and completion operation, or a validation/admission failure.
         */
        [[nodiscard]] Result<ResourceCreation<RenderMeshHandle>> ReplaceMesh(RenderMeshHandle current,
                                                                             const RenderMeshDescriptor &descriptor);

        /**
         * @brief Processes one bounded upload batch on the render-capable owner thread.
         * @return Number of completed requests, including typed backend failures.
         */
        [[nodiscard]] Result<std::size_t> ProcessResourceRequests();

        /** @brief Returns the current state of one buffer generation. */
        [[nodiscard]] Result<RenderResourceState> ResourceState(RenderBufferHandle buffer) const;

        /** @brief Returns the current state of one mesh generation. */
        [[nodiscard]] Result<RenderResourceState> ResourceState(RenderMeshHandle mesh) const;

        /** @brief Returns success, pending, or the stored typed result for one resource operation. */
        [[nodiscard]] Result<void> ResourceOperationResult(ResourceOperationId operation) const;

        /** @brief Logically releases one buffer generation; dependent meshes retain its native realization. */
        [[nodiscard]] Result<void> ReleaseBuffer(RenderBufferHandle buffer);

        /** @brief Logically releases one mesh generation and drains newly eligible dependencies. */
        [[nodiscard]] Result<void> ReleaseMesh(RenderMeshHandle mesh);

    private:
        friend class RenderFrameScope;

        class ConstructionKey {
            ConstructionKey() = default;
            friend class RenderFrontend;
        };

    public:
        RenderFrontend(std::unique_ptr<IRenderBackend> backend, RenderResourceOwnerId resourceOwner,
                       RenderResourceUploadLimits uploadLimits, ConstructionKey);

    private:
        [[nodiscard]] bool IsLiveTarget(RenderTargetHandle target, FramebufferExtent extent) const noexcept;
        [[nodiscard]] Result<void> ValidateMeshDependencies(const RenderMeshDescriptor &descriptor) const;
        [[nodiscard]] bool IsMeshBufferLayoutCompatible(const RenderMeshDescriptor &descriptor) const noexcept;

        struct TargetRecord {
            FramebufferExtent extent{};
        };

        struct BufferRecord {
            std::uint32_t generation{0};
            RenderBufferDescriptor descriptor;
        };

        std::unique_ptr<IRenderBackend> backend_;
        std::unique_ptr<Detail::RenderResourceRegistry> resourceRegistry_;
        std::unique_ptr<Detail::RenderResourceUploadQueue> resourceUploadQueue_;
        RenderFrameScope *activeFrameScope_{nullptr};
        IStaticMeshPassExecutor *staticMeshPassExecutor_{nullptr};
        std::vector<TargetRecord> targets_{{}};
        std::vector<BufferRecord> buffers_{{}};
    };
}  // namespace Horo::Render
