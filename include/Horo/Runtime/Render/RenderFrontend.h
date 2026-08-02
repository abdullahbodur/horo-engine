#pragma once

/**
 * @file RenderFrontend.h
 * @brief Host-facing owner of one selected and initialized renderer backend.
 */

#include "Horo/Runtime/Render/RenderBackendRegistry.h"

#include <memory>
#include <span>
#include <vector>

namespace Horo::Render
{
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
class RenderFrameScope final
{
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
class RenderFrontend final
{
  public:
    /**
     * @brief Creates and initializes the selected backend from a sealed registry.
     * @param registry Host-owned sealed backend registry.
     * @param backendId Canonical backend identity selected by host policy.
     * @param config Backend-neutral initialization policy.
     * @return Owned frontend, or the backend creation/initialization failure.
     */
    [[nodiscard]] static Result<std::unique_ptr<RenderFrontend>> Create(const RenderBackendRegistry &registry,
                                                                        const RenderBackendId &backendId,
                                                                        const RenderBackendConfig &config);

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
    [[nodiscard]] Result<void> SubmitFrame(const FrameDescriptor &descriptor,
                                           std::span<const RenderPassDescriptor> orderedPasses);

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
    void DetachStaticMeshPassExecutor(IStaticMeshPassExecutor &executor) noexcept;

    /** @brief Creates one logical offscreen target identity with an initial non-zero extent. */
    [[nodiscard]] Result<RenderTargetHandle> CreateOffscreenTarget(FramebufferExtent extent);

    /** @brief Updates the extent associated with a live generation-safe target handle. */
    [[nodiscard]] Result<void> ResizeOffscreenTarget(RenderTargetHandle target, FramebufferExtent extent);

    /** @brief Releases a live target and invalidates its generation; repeated stale release is rejected. */
    [[nodiscard]] Result<void> ReleaseOffscreenTarget(RenderTargetHandle target);

  private:
    friend class RenderFrameScope;

    explicit RenderFrontend(std::unique_ptr<IRenderBackend> backend) noexcept;
    [[nodiscard]] bool IsLiveTarget(RenderTargetHandle target, FramebufferExtent extent) const noexcept;

    struct TargetRecord
    {
        FramebufferExtent extent{};
        std::uint32_t generation{1};
        bool live{false};
    };

    std::unique_ptr<IRenderBackend> backend_;
    RenderFrameScope *activeFrameScope_{nullptr};
    IStaticMeshPassExecutor *staticMeshPassExecutor_{nullptr};
    std::vector<TargetRecord> targets_{{}};
};
} // namespace Horo::Render
