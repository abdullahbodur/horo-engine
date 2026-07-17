#include "Horo/Runtime/Render/RenderFrontend.h"

#include <string>
#include <utility>

namespace Horo::Render
{
    namespace
    {
        [[nodiscard]] Error MakeFrontendError(std::string code, std::string message)
        {
            return {
                ErrorCode{std::move(code)}, ErrorDomainId{"render.frontend"}, ErrorSeverity::Error, std::move(message),
                {}
            };
        }
    } // namespace

    /** @copydoc RenderFrameScope::~RenderFrameScope */
    RenderFrameScope::~RenderFrameScope()
    {
        Abort();
    }

    /** @copydoc RenderFrameScope::RenderFrameScope(RenderFrameScope&&) */
    RenderFrameScope::RenderFrameScope(RenderFrameScope&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr)), backend_(std::exchange(other.backend_, nullptr)),
          frame_(std::exchange(other.frame_, {})), executed_(std::exchange(other.executed_, false))
    {
        if (owner_ != nullptr)
        {
            owner_->activeFrameScope_ = this;
        }
    }

    /** @copydoc RenderFrameScope::operator=(RenderFrameScope&&) */
    RenderFrameScope& RenderFrameScope::operator=(RenderFrameScope&& other) noexcept
    {
        if (this != &other)
        {
            Abort();
            owner_ = std::exchange(other.owner_, nullptr);
            backend_ = std::exchange(other.backend_, nullptr);
            frame_ = std::exchange(other.frame_, {});
            executed_ = std::exchange(other.executed_, false);
            if (owner_ != nullptr)
            {
                owner_->activeFrameScope_ = this;
            }
        }
        return *this;
    }

    /** @copydoc RenderFrameScope::Execute */
    Result<void> RenderFrameScope::Execute(const std::span<const RenderPassDescriptor> orderedPasses)
    {
        if (backend_ == nullptr)
        {
            return Result<void>::Failure(
                MakeFrontendError("render.frontend.frame_not_active", "Renderer frame scope no longer owns a frame."));
        }
        if (executed_)
        {
            return Result<void>::Failure(MakeFrontendError("render.frontend.frame_already_executed",
                                                           "Renderer frame scope has already executed its pass sequence."));
        }

        try
        {
            for (const RenderPassDescriptor& pass : orderedPasses)
            {
                if (pass.primaryOutput.has_value() && pass.staticMesh.has_value())
                {
                    Abort();
                    return Result<void>::Failure(MakeFrontendError(
                        "render.frontend.ambiguous_pass_workload",
                        "A render pass cannot bind primary-output and static-mesh workloads together."));
                }
                if (!pass.staticMesh.has_value())
                {
                    continue;
                }
                if (owner_->staticMeshPassExecutor_ == nullptr)
                {
                    Abort();
                    return Result<void>::Failure(MakeFrontendError(
                        "render.frontend.static_mesh_executor_missing",
                        "Static-mesh pass requires an attached backend executor."));
                }
                if (!pass.staticMesh->IsValid())
                {
                    Abort();
                    return Result<void>::Failure(MakeFrontendError(
                        "render.frontend.invalid_static_mesh_pass", "Static-mesh pass descriptor is invalid."));
                }
                if (!owner_->IsLiveTarget(pass.staticMesh->target, pass.staticMesh->extent))
                {
                    Abort();
                    return Result<void>::Failure(MakeFrontendError(
                        "render.frontend.stale_render_target",
                        "Static-mesh pass references a stale target or mismatched target extent."));
                }
                const Result<void> staticMeshExecuted =
                    owner_->staticMeshPassExecutor_->ExecuteStaticMeshPass(*pass.staticMesh);
                if (staticMeshExecuted.HasError())
                {
                    Abort();
                    return Result<void>::Failure(staticMeshExecuted.ErrorValue());
                }
            }
            const Result<void> executed =
                backend_->Execute(RenderExecutionPlan{.frame = frame_, .orderedPasses = orderedPasses});
            if (executed.HasError())
            {
                Abort();
                return Result<void>::Failure(executed.ErrorValue());
            }
            executed_ = true;
            return Result<void>::Success();
        }
        catch (...)
        {
            Abort();
            return Result<void>::Failure(
                MakeFrontendError("render.frontend.frame_exception", "Renderer backend frame operation threw."));
        }
    }

    /** @copydoc RenderFrameScope::Present */
    Result<void> RenderFrameScope::Present()
    {
        if (backend_ == nullptr)
        {
            return Result<void>::Failure(
                MakeFrontendError("render.frontend.frame_not_active", "Renderer frame scope no longer owns a frame."));
        }
        if (!executed_)
        {
            return Result<void>::Failure(MakeFrontendError("render.frontend.frame_not_executed",
                                                           "Renderer frame scope must execute before presentation."));
        }

        try
        {
            const Result<void> presented = backend_->Present(frame_);
            if (presented.HasError())
            {
                Abort();
                return Result<void>::Failure(presented.ErrorValue());
            }
            Release();
            return Result<void>::Success();
        }
        catch (...)
        {
            Abort();
            return Result<void>::Failure(
                MakeFrontendError("render.frontend.frame_exception", "Renderer backend frame operation threw."));
        }
    }

    /** @copydoc RenderFrameScope::Cancel */
    void RenderFrameScope::Cancel() noexcept
    {
        Abort();
    }

    RenderFrameScope::RenderFrameScope(RenderFrontend& owner, IRenderBackend& backend, const FrameToken frame) noexcept
        : owner_(&owner), backend_(&backend), frame_(frame)
    {
        owner.activeFrameScope_ = this;
    }

    void RenderFrameScope::Abort() noexcept
    {
        if (backend_ != nullptr)
        {
            backend_->AbortFrame(frame_);
        }
        Release();
    }

    void RenderFrameScope::Release() noexcept
    {
        if (owner_ != nullptr && owner_->activeFrameScope_ == this)
        {
            owner_->activeFrameScope_ = nullptr;
        }
        owner_ = nullptr;
        backend_ = nullptr;
        frame_ = {};
        executed_ = false;
    }

    /** @copydoc RenderFrontend::Create */
    Result<std::unique_ptr<RenderFrontend>> RenderFrontend::Create(const RenderBackendRegistry& registry,
                                                                   const RenderBackendId& backendId,
                                                                   const RenderBackendConfig& config)
    {
        auto createdBackend = registry.Create(backendId);
        if (createdBackend.HasError())
        {
            return Result<std::unique_ptr<RenderFrontend>>::Failure(createdBackend.ErrorValue());
        }

        std::unique_ptr<IRenderBackend> backend = std::move(createdBackend).Value();
        try
        {
            const Result<void> initialized = backend->Initialize(config);
            if (initialized.HasError())
            {
                backend->Shutdown();
                return Result<std::unique_ptr<RenderFrontend>>::Failure(initialized.ErrorValue());
            }
        }
        catch (...)
        {
            backend->Shutdown();
            return Result<std::unique_ptr<RenderFrontend>>::Failure(
                MakeFrontendError("render.frontend.initialize_exception", "Renderer backend initialization threw."));
        }

        return Result<std::unique_ptr<RenderFrontend>>::Success(
            std::unique_ptr<RenderFrontend>{new RenderFrontend(std::move(backend))});
    }

    /** @copydoc RenderFrontend::~RenderFrontend */
    RenderFrontend::~RenderFrontend()
    {
        if (activeFrameScope_ != nullptr)
        {
            activeFrameScope_->Abort();
        }
        backend_->Shutdown();
    }

    /** @copydoc RenderFrontend::Capabilities */
    const RenderBackendCapabilities& RenderFrontend::Capabilities() const noexcept
    {
        return backend_->Capabilities();
    }

    /** @copydoc RenderFrontend::BeginFrame */
    Result<RenderFrameScope> RenderFrontend::BeginFrame(const FrameDescriptor& descriptor)
    {
        if (activeFrameScope_ != nullptr)
        {
            return Result<RenderFrameScope>::Failure(MakeFrontendError(
                "render.frontend.frame_already_active", "Renderer frontend already owns an active frame scope."));
        }

        try
        {
            auto begun = backend_->BeginFrame(descriptor);
            if (begun.HasError())
            {
                backend_->AbortActiveFrame();
                return Result<RenderFrameScope>::Failure(begun.ErrorValue());
            }

            const FrameToken frame = begun.Value();
            if (!frame.IsValid())
            {
                backend_->AbortActiveFrame();
                return Result<RenderFrameScope>::Failure(MakeFrontendError(
                    "render.frontend.invalid_frame_token", "Renderer backend returned an invalid frame token."));
            }
            return Result<RenderFrameScope>::Success(RenderFrameScope{*this, *backend_, frame});
        }
        catch (...)
        {
            backend_->AbortActiveFrame();
            return Result<RenderFrameScope>::Failure(
                MakeFrontendError("render.frontend.frame_exception", "Renderer backend frame operation threw."));
        }
    }

    /** @copydoc RenderFrontend::SubmitFrame */
    Result<void> RenderFrontend::SubmitFrame(const FrameDescriptor& descriptor,
                                             const std::span<const RenderPassDescriptor> orderedPasses)
    {
        auto begun = BeginFrame(descriptor);
        if (begun.HasError())
        {
            return Result<void>::Failure(begun.ErrorValue());
        }

        RenderFrameScope frame = std::move(begun).Value();
        const Result<void> executed = frame.Execute(orderedPasses);
        if (executed.HasError())
        {
            return Result<void>::Failure(executed.ErrorValue());
        }
        return frame.Present();
    }

    /** @copydoc RenderFrontend::Resize */
    Result<void> RenderFrontend::Resize(const FramebufferExtent extent)
    {
        if (activeFrameScope_ != nullptr)
        {
            return Result<void>::Failure(MakeFrontendError("render.frontend.resize_during_frame",
                                                           "Renderer output cannot be resized during an active frame."));
        }

        try
        {
            return backend_->Resize(extent);
        }
        catch (...)
        {
            return Result<void>::Failure(
                MakeFrontendError("render.frontend.resize_exception", "Renderer backend resize operation threw."));
        }
    }

    /** @copydoc RenderFrontend::AttachStaticMeshPassExecutor */
    Result<void> RenderFrontend::AttachStaticMeshPassExecutor(IStaticMeshPassExecutor& executor)
    {
        if (activeFrameScope_ != nullptr)
        {
            return Result<void>::Failure(MakeFrontendError(
                "render.frontend.executor_change_during_frame", "Render pass executor cannot change during a frame."));
        }
        if (staticMeshPassExecutor_ != nullptr && staticMeshPassExecutor_ != &executor)
        {
            return Result<void>::Failure(MakeFrontendError(
                "render.frontend.static_mesh_executor_already_attached",
                "A static-mesh pass executor is already attached."));
        }
        staticMeshPassExecutor_ = &executor;
        return Result<void>::Success();
    }

    /** @copydoc RenderFrontend::DetachStaticMeshPassExecutor */
    void RenderFrontend::DetachStaticMeshPassExecutor(IStaticMeshPassExecutor& executor) noexcept
    {
        if (activeFrameScope_ == nullptr && staticMeshPassExecutor_ == &executor)
        {
            staticMeshPassExecutor_ = nullptr;
        }
    }

    /** @copydoc RenderFrontend::CreateOffscreenTarget */
    Result<RenderTargetHandle> RenderFrontend::CreateOffscreenTarget(const FramebufferExtent extent)
    {
        if (!extent.IsValid())
            return Result<RenderTargetHandle>::Failure(
                MakeFrontendError("render.frontend.invalid_target_extent", "Offscreen target extent is invalid."));
        for (std::size_t index = 1; index < targets_.size(); ++index)
        {
            TargetRecord& record = targets_[index];
            if (!record.live)
            {
                record.live = true;
                record.extent = extent;
                return Result<RenderTargetHandle>::Success(
                    RenderTargetHandle{static_cast<std::uint32_t>(index), record.generation});
            }
        }
        targets_.push_back(TargetRecord{.extent = extent, .generation = 1, .live = true});
        return Result<RenderTargetHandle>::Success(
            RenderTargetHandle{static_cast<std::uint32_t>(targets_.size() - 1), 1});
    }

    /** @copydoc RenderFrontend::ResizeOffscreenTarget */
    Result<void> RenderFrontend::ResizeOffscreenTarget(const RenderTargetHandle target,
                                                       const FramebufferExtent extent)
    {
        if (!extent.IsValid() || target.index >= targets_.size() || !targets_[target.index].live ||
            targets_[target.index].generation != target.generation)
            return Result<void>::Failure(MakeFrontendError(
                "render.frontend.stale_render_target", "Offscreen target handle or extent is invalid."));
        targets_[target.index].extent = extent;
        return Result<void>::Success();
    }

    /** @copydoc RenderFrontend::ReleaseOffscreenTarget */
    Result<void> RenderFrontend::ReleaseOffscreenTarget(const RenderTargetHandle target)
    {
        if (activeFrameScope_ != nullptr)
            return Result<void>::Failure(MakeFrontendError(
                "render.frontend.target_release_during_frame", "Offscreen target cannot be released during a frame."));
        if (target.index >= targets_.size() || !targets_[target.index].live ||
            targets_[target.index].generation != target.generation)
            return Result<void>::Failure(MakeFrontendError(
                "render.frontend.stale_render_target", "Offscreen target handle is stale."));
        TargetRecord& record = targets_[target.index];
        record.live = false;
        record.extent = {};
        ++record.generation;
        if (record.generation == 0)
            ++record.generation;
        return Result<void>::Success();
    }

    bool RenderFrontend::IsLiveTarget(const RenderTargetHandle target, const FramebufferExtent extent) const noexcept
    {
        return target.index < targets_.size() && targets_[target.index].live &&
               targets_[target.index].generation == target.generation &&
               targets_[target.index].extent.width == extent.width &&
               targets_[target.index].extent.height == extent.height;
    }

    RenderFrontend::RenderFrontend(std::unique_ptr<IRenderBackend> backend) noexcept : backend_(std::move(backend))
    {
    }
} // namespace Horo::Render
