#include "Horo/Runtime/Render/RenderFrontend.h"

#include "RenderFrontendErrors.h"
#include "RenderResourceOperations.h"
#include "RenderResourceRegistry.h"
#include "RenderResourceUploadQueue.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace Horo::Render {
    namespace {
        [[nodiscard]] Error MakeFrontendError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

    }  // namespace

    /** @copydoc RenderFrameScope::~RenderFrameScope */
    RenderFrameScope::~RenderFrameScope() {
        Abort();
    }

    /** @copydoc RenderFrameScope::RenderFrameScope(RenderFrameScope&&) */
    RenderFrameScope::RenderFrameScope(RenderFrameScope &&other) noexcept
        : owner_(std::exchange(other.owner_, nullptr)), backend_(std::exchange(other.backend_, nullptr)),
          frame_(std::exchange(other.frame_, {})), executed_(std::exchange(other.executed_, false)) {
        if (owner_ != nullptr) {
            owner_->activeFrameScope_ = this;
        }
    }

    /** @copydoc RenderFrameScope::operator=(RenderFrameScope&&) */
    RenderFrameScope &RenderFrameScope::operator=(RenderFrameScope &&other) noexcept {
        if (this != &other) {
            Abort();
            owner_ = std::exchange(other.owner_, nullptr);
            backend_ = std::exchange(other.backend_, nullptr);
            frame_ = std::exchange(other.frame_, {});
            executed_ = std::exchange(other.executed_, false);
            if (owner_ != nullptr) {
                owner_->activeFrameScope_ = this;
            }
        }
        return *this;
    }

    /** @copydoc RenderFrameScope::Execute */
    Result<void> RenderFrameScope::Execute(const std::span<const RenderPassDescriptor> orderedPasses) {
        if (backend_ == nullptr) {
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::FrameNotActive, "Renderer frame scope no longer owns a frame."));
        }
        if (executed_) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::FrameAlreadyExecuted, "Renderer frame scope has already executed its pass sequence."));
        }

        try {
            for (const RenderPassDescriptor &pass : orderedPasses) {
                if (pass.primaryOutput.has_value() && pass.staticMesh.has_value()) {
                    Abort();
                    return Result<void>::Failure(
                        MakeFrontendError(FrontendErrors::AmbiguousPassWorkload,
                                          "A render pass cannot bind primary-output and static-mesh workloads together."));
                }
                if (!pass.staticMesh.has_value()) {
                    continue;
                }
                if (owner_->staticMeshPassExecutor_ == nullptr) {
                    Abort();
                    return Result<void>::Failure(MakeFrontendError(FrontendErrors::StaticMeshExecutorMissing,
                                                                   "Static-mesh pass requires an attached backend executor."));
                }
                if (!pass.staticMesh->IsValid()) {
                    Abort();
                    return Result<void>::Failure(
                        MakeFrontendError(FrontendErrors::InvalidStaticMeshPass, "Static-mesh pass descriptor is invalid."));
                }
                if (!owner_->IsLiveTarget(pass.staticMesh->target, pass.staticMesh->extent)) {
                    Abort();
                    return Result<void>::Failure(
                        MakeFrontendError(FrontendErrors::StaleRenderTarget,
                                          "Static-mesh pass references a stale target or mismatched target extent."));
                }
                const Result<void> staticMeshExecuted = owner_->staticMeshPassExecutor_->ExecuteStaticMeshPass(*pass.staticMesh);
                if (staticMeshExecuted.HasError()) {
                    Abort();
                    return Result<void>::Failure(staticMeshExecuted.ErrorValue());
                }
            }
            if (const Result<void> executed = backend_->Execute(RenderExecutionPlan{.frame = frame_, .orderedPasses = orderedPasses});
                executed.HasError()) {
                Abort();
                return Result<void>::Failure(executed.ErrorValue());
            }
            executed_ = true;
            return Result<void>::Success();
        } catch (...) {  // NOSONAR(cpp:S2738)
            Abort();
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::FrameException, "Renderer backend frame operation threw."));
        }
    }

    /** @copydoc RenderFrameScope::Present */
    Result<void> RenderFrameScope::Present() {
        if (backend_ == nullptr) {
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::FrameNotActive, "Renderer frame scope no longer owns a frame."));
        }
        if (!executed_) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::FrameNotExecuted, "Renderer frame scope must execute before presentation."));
        }

        try {
            if (const Result<void> presented = backend_->Present(frame_); presented.HasError()) {
                Abort();
                return Result<void>::Failure(presented.ErrorValue());
            }
            Release();
            return Result<void>::Success();
        } catch (...) {  // NOSONAR(cpp:S2738)
            Abort();
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::FrameException, "Renderer backend frame operation threw."));
        }
    }

    /** @copydoc RenderFrameScope::Cancel */
    void RenderFrameScope::Cancel() noexcept {
        Abort();
    }

    RenderFrameScope::RenderFrameScope(RenderFrontend &owner, IRenderBackend &backend, const FrameToken frame) noexcept
        : owner_(&owner), backend_(&backend), frame_(frame) {
        owner.activeFrameScope_ = this;
    }

    void RenderFrameScope::Abort() noexcept {
        if (backend_ != nullptr) {
            backend_->AbortFrame(frame_);
        }
        Release();
    }

    void RenderFrameScope::Release() noexcept {
        if (owner_ != nullptr && owner_->activeFrameScope_ == this) {
            owner_->activeFrameScope_ = nullptr;
        }
        owner_ = nullptr;
        backend_ = nullptr;
        frame_ = {};
        executed_ = false;
    }

    /** @copydoc RenderFrontend::Create */
    Result<std::unique_ptr<RenderFrontend>> RenderFrontend::Create(const RenderBackendRegistry &registry, const RenderBackendId &backendId,
                                                                   const RenderBackendConfig &config,
                                                                   const RenderResourceUploadLimits uploadLimits) {
        if (!uploadLimits.IsValid()) {
            return Result<std::unique_ptr<RenderFrontend>>::Failure(
                MakeFrontendError(FrontendErrors::InvalidResourceUploadLimits, "Renderer resource upload limits are invalid."));
        }
        auto createdBackend = registry.Create(backendId);
        if (createdBackend.HasError()) {
            return Result<std::unique_ptr<RenderFrontend>>::Failure(createdBackend.ErrorValue());
        }

        std::unique_ptr<IRenderBackend> backend = std::move(createdBackend).Value();
        try {
            if (const Result<void> initialized = backend->Initialize(config); initialized.HasError()) {
                backend->Shutdown();
                return Result<std::unique_ptr<RenderFrontend>>::Failure(initialized.ErrorValue());
            }
        } catch (...) {  // NOSONAR(cpp:S2738)
            backend->Shutdown();
            return Result<std::unique_ptr<RenderFrontend>>::Failure(
                MakeFrontendError(FrontendErrors::InitializeException, "Renderer backend initialization threw."));
        }

        auto resourceOwner = Detail::AcquireRenderResourceOwnerId();
        if (resourceOwner.HasError()) {
            backend->Shutdown();
            return Result<std::unique_ptr<RenderFrontend>>::Failure(resourceOwner.ErrorValue());
        }
        return Result<std::unique_ptr<RenderFrontend>>::Success(
            std::make_unique<RenderFrontend>(std::move(backend), resourceOwner.Value(), uploadLimits, ConstructionKey{}));
    }

    RenderFrontend::RenderFrontend(std::unique_ptr<IRenderBackend> backend, const RenderResourceOwnerId resourceOwner,
                                   const RenderResourceUploadLimits uploadLimits, ConstructionKey)
        : backend_(std::move(backend)),
          resourceRegistry_(std::make_unique<Detail::RenderResourceRegistry>(resourceOwner, Detail::RenderResourceRegistryLimits{},
                                                                             [this](const Detail::RenderResourceClass resourceClass,
                                                                                    const std::uint64_t backendInstance) {
                                                                                 if (resourceClass == Detail::RenderResourceClass::Buffer) {
                                                                                     backend_->DestroyBuffer(backendInstance);
                                                                                 } else if (resourceClass ==
                                                                                            Detail::RenderResourceClass::Mesh) {
                                                                                     backend_->DestroyMesh(backendInstance);
                                                                                 }
                                                                             })),
          resourceUploadQueue_(std::make_unique<Detail::RenderResourceUploadQueue>(uploadLimits)) {}

    /** @copydoc RenderFrontend::~RenderFrontend */
    RenderFrontend::~RenderFrontend() {
        if (activeFrameScope_ != nullptr) {
            activeFrameScope_->Abort();
        }
        resourceUploadQueue_->Clear();
        resourceRegistry_->Shutdown();
        backend_->Shutdown();
    }

    /** @copydoc RenderFrontend::Capabilities */
    const RenderBackendCapabilities &RenderFrontend::Capabilities() const noexcept {
        return backend_->Capabilities();
    }

    /** @copydoc RenderFrontend::BeginFrame */
    Result<RenderFrameScope> RenderFrontend::BeginFrame(const FrameDescriptor &descriptor) {
        if (activeFrameScope_ != nullptr) {
            return Result<RenderFrameScope>::Failure(
                MakeFrontendError(FrontendErrors::FrameAlreadyActive, "Renderer frontend already owns an active frame scope."));
        }

        if (const Result<std::size_t> processed = ProcessResourceRequests(); processed.HasError()) {
            return Result<RenderFrameScope>::Failure(processed.ErrorValue());
        }

        try {
            auto begun = backend_->BeginFrame(descriptor);
            if (begun.HasError()) {
                backend_->AbortActiveFrame();
                return Result<RenderFrameScope>::Failure(begun.ErrorValue());
            }

            const FrameToken frame = begun.Value();
            if (!frame.IsValid()) {
                backend_->AbortActiveFrame();
                return Result<RenderFrameScope>::Failure(
                    MakeFrontendError(FrontendErrors::InvalidFrameToken, "Renderer backend returned an invalid frame token."));
            }
            return Result<RenderFrameScope>::Success(RenderFrameScope{*this, *backend_, frame});
        } catch (...) {  // NOSONAR(cpp:S2738)
            backend_->AbortActiveFrame();
            return Result<RenderFrameScope>::Failure(
                MakeFrontendError(FrontendErrors::FrameException, "Renderer backend frame operation threw."));
        }
    }

    /** @copydoc RenderFrontend::SubmitFrame */
    Result<void> RenderFrontend::SubmitFrame(const FrameDescriptor &descriptor, const std::span<const RenderPassDescriptor> orderedPasses) {
        auto begun = BeginFrame(descriptor);
        if (begun.HasError()) {
            return Result<void>::Failure(begun.ErrorValue());
        }

        RenderFrameScope frame = std::move(begun).Value();
        if (const Result<void> executed = frame.Execute(orderedPasses); executed.HasError()) {
            return Result<void>::Failure(executed.ErrorValue());
        }
        return frame.Present();
    }

    /** @copydoc RenderFrontend::Resize */
    Result<void> RenderFrontend::Resize(const FramebufferExtent extent) {
        if (activeFrameScope_ != nullptr) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResizeDuringFrame, "Renderer output cannot be resized during an active frame."));
        }

        try {
            return backend_->Resize(extent);
        } catch (...) {  // NOSONAR(cpp:S2738)
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::ResizeException, "Renderer backend resize operation threw."));
        }
    }

    /** @copydoc RenderFrontend::AttachStaticMeshPassExecutor */
    Result<void> RenderFrontend::AttachStaticMeshPassExecutor(IStaticMeshPassExecutor &executor) {
        if (activeFrameScope_ != nullptr) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ExecutorChangeDuringFrame, "Render pass executor cannot change during a frame."));
        }
        if (staticMeshPassExecutor_ != nullptr && staticMeshPassExecutor_ != &executor) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::StaticMeshExecutorAlreadyAttached, "A static-mesh pass executor is already attached."));
        }
        staticMeshPassExecutor_ = &executor;
        return Result<void>::Success();
    }

    /** @copydoc RenderFrontend::DetachStaticMeshPassExecutor */
    void RenderFrontend::DetachStaticMeshPassExecutor(const IStaticMeshPassExecutor &executor) noexcept {
        if (activeFrameScope_ == nullptr && staticMeshPassExecutor_ == &executor) {
            staticMeshPassExecutor_ = nullptr;
        }
    }

    /** @copydoc RenderFrontend::CreateOffscreenTarget */
    Result<RenderTargetHandle> RenderFrontend::CreateOffscreenTarget(const FramebufferExtent extent) {
        if (!extent.IsValid())
            return Result<RenderTargetHandle>::Failure(
                MakeFrontendError(FrontendErrors::InvalidTargetExtent, "Offscreen target extent is invalid."));
        auto reservation = resourceRegistry_->Reserve(Detail::RenderResourceClass::RenderTarget);
        if (reservation.HasError()) {
            return Result<RenderTargetHandle>::Failure(reservation.ErrorValue());
        }
        const Detail::RenderResourceIdentity identity = reservation.Value().identity;
        if (identity.slot >= targets_.size()) {
            targets_.resize(static_cast<std::size_t>(identity.slot) + 1);
        }
        targets_[identity.slot].extent = extent;
        if (const Result<void> published = resourceRegistry_->Publish(Detail::RenderResourceClass::RenderTarget, identity, identity.slot);
            published.HasError()) {
            const Error publicationError = published.ErrorValue();
            static_cast<void>(resourceRegistry_->Fail(Detail::RenderResourceClass::RenderTarget, identity, publicationError));
            static_cast<void>(resourceRegistry_->DrainRetirements());
            targets_[identity.slot] = {};
            return Result<RenderTargetHandle>::Failure(publicationError);
        }
        return Result<RenderTargetHandle>::Success(RenderTargetHandle{identity.owner, identity.slot, identity.generation});
    }

    /** @copydoc RenderFrontend::ResizeOffscreenTarget */
    Result<void> RenderFrontend::ResizeOffscreenTarget(const RenderTargetHandle target, const FramebufferExtent extent) {
        if (!extent.IsValid()) {
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::InvalidTargetExtent, "Offscreen target extent is invalid."));
        }
        const Detail::RenderResourceIdentity identity{target.owner, target.slot, target.generation};
        const auto state = resourceRegistry_->State(Detail::RenderResourceClass::RenderTarget, identity);
        if (state.HasError()) {
            return Result<void>::Failure(state.ErrorValue());
        }
        if (state.Value() != RenderResourceState::Ready) {
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::ResourceNotReady, "Offscreen target is not ready for resize."));
        }
        targets_[target.slot].extent = extent;
        return Result<void>::Success();
    }

    /** @copydoc RenderFrontend::ReleaseOffscreenTarget */
    Result<void> RenderFrontend::ReleaseOffscreenTarget(const RenderTargetHandle target) {
        if (activeFrameScope_ != nullptr)
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::TargetReleaseDuringFrame, "Offscreen target cannot be released during a frame."));
        const Detail::RenderResourceIdentity identity{target.owner, target.slot, target.generation};
        if (const Result<void> released = resourceRegistry_->Release(Detail::RenderResourceClass::RenderTarget, identity);
            released.HasError()) {
            return Result<void>::Failure(released.ErrorValue());
        }
        targets_[target.slot] = {};
        static_cast<void>(resourceRegistry_->DrainRetirements());
        return Result<void>::Success();
    }

    /** @copydoc RenderFrontend::CreateBuffer */
    Result<ResourceCreation<RenderBufferHandle>> RenderFrontend::CreateBuffer(const RenderBufferDescriptor &descriptor,
                                                                              const std::span<const std::byte> initialData) {
        if (activeFrameScope_ != nullptr) {
            return Result<ResourceCreation<RenderBufferHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A buffer cannot be created during an active frame."));
        }
        if (!descriptor.IsValid()) {
            return Result<ResourceCreation<RenderBufferHandle>>::Failure(
                MakeFrontendError(FrontendErrors::InvalidBufferDescriptor, "The buffer descriptor is structurally invalid."));
        }
        if (!backend_->Capabilities().supportsBufferResources) {
            return Result<ResourceCreation<RenderBufferHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceUnsupported, "The active renderer backend does not support generic buffers."));
        }
        if (initialData.size() != descriptor.byteSize) {
            return Result<ResourceCreation<RenderBufferHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceBufferUploadSizeMismatch,
                                  "The initial-data byte count must equal the buffer descriptor size."));
        }
        if (!resourceUploadQueue_->CanEnqueue(initialData.size())) {
            return Result<ResourceCreation<RenderBufferHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceUploadCapacityExceeded,
                                  "The bounded initial-upload byte queue has insufficient capacity."));
        }

        auto reserved = resourceRegistry_->Reserve(Detail::RenderResourceClass::Buffer);
        if (reserved.HasError()) {
            return Result<ResourceCreation<RenderBufferHandle>>::Failure(reserved.ErrorValue());
        }
        const Detail::ResourceReservation reservation = reserved.Value();
        if (reservation.identity.slot >= buffers_.size()) {
            buffers_.resize(static_cast<std::size_t>(reservation.identity.slot) + 1);
        }
        buffers_[reservation.identity.slot] = {.generation = reservation.identity.generation, .descriptor = descriptor};
        resourceUploadQueue_->EnqueueBuffer(reservation.identity, descriptor, initialData);
        return Result<ResourceCreation<RenderBufferHandle>>::Success(
            {.handle = BufferHandle(reservation.identity), .operation = reservation.operation});
    }

    /** @copydoc RenderFrontend::CreateMesh */
    Result<ResourceCreation<RenderMeshHandle>> RenderFrontend::CreateMesh(const RenderMeshDescriptor &descriptor) {
        if (activeFrameScope_ != nullptr) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A mesh cannot be created during an active frame."));
        }
        if (!descriptor.IsValid()) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(
                MakeFrontendError(FrontendErrors::InvalidMeshDescriptor, "The mesh descriptor is structurally invalid."));
        }
        if (!backend_->Capabilities().supportsMeshResources) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceUnsupported, "The active renderer backend does not support generic meshes."));
        }

        if (const Result<void> dependencies = ValidateMeshDependencies(descriptor); dependencies.HasError()) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(dependencies.ErrorValue());
        }
        if (!IsMeshBufferLayoutCompatible(descriptor)) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(
                MakeFrontendError(FrontendErrors::InvalidMeshDescriptor,
                                  "Mesh layout or counts are incompatible with the referenced buffers."));
        }

        const std::array dependencies{Identity(descriptor.vertexBuffer), Identity(descriptor.indexBuffer)};
        auto reserved = resourceRegistry_->Reserve(Detail::RenderResourceClass::Mesh, dependencies);
        if (reserved.HasError()) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(reserved.ErrorValue());
        }
        const Detail::ResourceReservation reservation = reserved.Value();
        resourceUploadQueue_->EnqueueMesh(reservation.identity, descriptor, std::nullopt);
        return Result<ResourceCreation<RenderMeshHandle>>::Success(
            {.handle = MeshHandle(reservation.identity), .operation = reservation.operation});
    }

    /** @copydoc RenderFrontend::ReplaceMesh */
    Result<ResourceCreation<RenderMeshHandle>> RenderFrontend::ReplaceMesh(const RenderMeshHandle current,
                                                                           const RenderMeshDescriptor &descriptor) {
        const auto state = ResourceState(current);
        if (state.HasError()) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(state.ErrorValue());
        }
        if (state.Value() != RenderResourceState::Ready) {
            return Result<ResourceCreation<RenderMeshHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceNotReady, "Only a ready mesh generation can be replaced."));
        }
        auto replacement = CreateMesh(descriptor);
        if (replacement.HasValue()) {
            resourceUploadQueue_->MarkBackAsReplacement(Identity(current));
        }
        return replacement;
    }

    /** @copydoc RenderFrontend::ProcessResourceRequests */
    Result<std::size_t> RenderFrontend::ProcessResourceRequests() {
        if (activeFrameScope_ != nullptr) {
            return Result<std::size_t>::Failure(MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame,
                                                                  "Resource requests can only be processed at a frame boundary."));
        }

        std::size_t completedRequests = 0;
        std::size_t completedBytes = 0;
        while (!resourceUploadQueue_->Empty() && !resourceUploadQueue_->DrainLimitReached(completedRequests, completedBytes)) {
            Detail::RenderResourceUploadQueue::Request request = resourceUploadQueue_->Pop();
            completedBytes += request.initialData.size();
            const Result<std::uint64_t> created = RealizeResourceRequest(*backend_, *resourceRegistry_, request);
            CompleteResourceRequest(*backend_, *resourceRegistry_, request, created);
            ++completedRequests;
        }
        static_cast<void>(resourceRegistry_->DrainRetirements());
        return Result<std::size_t>::Success(completedRequests);
    }

    /** @copydoc RenderFrontend::ResourceState(RenderBufferHandle) */
    Result<RenderResourceState> RenderFrontend::ResourceState(const RenderBufferHandle buffer) const {
        return resourceRegistry_->State(Detail::RenderResourceClass::Buffer, Identity(buffer));
    }

    /** @copydoc RenderFrontend::ResourceState(RenderMeshHandle) */
    Result<RenderResourceState> RenderFrontend::ResourceState(const RenderMeshHandle mesh) const {
        return resourceRegistry_->State(Detail::RenderResourceClass::Mesh, Identity(mesh));
    }

    /** @copydoc RenderFrontend::ResourceOperationResult */
    Result<void> RenderFrontend::ResourceOperationResult(const ResourceOperationId operation) const {
        return resourceRegistry_->OperationResult(operation);
    }

    /** @copydoc RenderFrontend::ReleaseBuffer */
    Result<void> RenderFrontend::ReleaseBuffer(const RenderBufferHandle buffer) {
        if (activeFrameScope_ != nullptr) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A buffer cannot be released during an active frame."));
        }
        const Result<void> released = resourceRegistry_->Release(Detail::RenderResourceClass::Buffer, Identity(buffer));
        if (released.HasValue()) {
            static_cast<void>(resourceRegistry_->DrainRetirements());
        }
        return released;
    }

    /** @copydoc RenderFrontend::ReleaseMesh */
    Result<void> RenderFrontend::ReleaseMesh(const RenderMeshHandle mesh) {
        if (activeFrameScope_ != nullptr) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A mesh cannot be released during an active frame."));
        }
        const Result<void> released = resourceRegistry_->Release(Detail::RenderResourceClass::Mesh, Identity(mesh));
        if (released.HasValue()) {
            static_cast<void>(resourceRegistry_->DrainRetirements());
        }
        return released;
    }

    Result<void> RenderFrontend::ValidateMeshDependencies(const RenderMeshDescriptor &descriptor) const {
        const auto vertex = resourceRegistry_->State(Detail::RenderResourceClass::Buffer, Identity(descriptor.vertexBuffer));
        if (vertex.HasError()) {
            return Result<void>::Failure(vertex.ErrorValue());
        }
        const auto index = resourceRegistry_->State(Detail::RenderResourceClass::Buffer, Identity(descriptor.indexBuffer));
        if (index.HasError()) {
            return Result<void>::Failure(index.ErrorValue());
        }
        if (vertex.Value() != RenderResourceState::Ready || index.Value() != RenderResourceState::Ready) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceDependencyNotReady, "Mesh creation requires ready vertex and index buffers."));
        }
        return Result<void>::Success();
    }

    bool RenderFrontend::IsMeshBufferLayoutCompatible(const RenderMeshDescriptor &descriptor) const noexcept {
        if (descriptor.vertexBuffer.slot >= buffers_.size() || descriptor.indexBuffer.slot >= buffers_.size()) {
            return false;
        }
        const BufferRecord &vertex = buffers_[descriptor.vertexBuffer.slot];
        const BufferRecord &index = buffers_[descriptor.indexBuffer.slot];
        const std::uint32_t indexSize = descriptor.indexFormat == RenderIndexFormat::UInt16 ? 2U : 4U;
        const bool recordsMatch =
            vertex.generation == descriptor.vertexBuffer.generation && index.generation == descriptor.indexBuffer.generation;
        return recordsMatch && HasBufferUsage(vertex.descriptor.usage, RenderBufferUsage::Vertex) &&
               HasBufferUsage(index.descriptor.usage, RenderBufferUsage::Index) &&
               FitsBuffer(descriptor.vertexStride, descriptor.vertexCount, vertex.descriptor.byteSize) &&
               FitsBuffer(indexSize, descriptor.indexCount, index.descriptor.byteSize);
    }

    bool RenderFrontend::IsLiveTarget(const RenderTargetHandle target, const FramebufferExtent extent) const noexcept {
        const auto state =
            resourceRegistry_->State(Detail::RenderResourceClass::RenderTarget, {target.owner, target.slot, target.generation});
        return state.HasValue() && state.Value() == RenderResourceState::Ready && target.slot < targets_.size() &&
               targets_[target.slot].extent.width == extent.width && targets_[target.slot].extent.height == extent.height;
    }
}  // namespace Horo::Render
