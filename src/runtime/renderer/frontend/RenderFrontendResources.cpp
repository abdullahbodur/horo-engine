#include "Horo/Runtime/Render/RenderFrontend.h"
#include "RenderFrontendErrors.h"
#include "RenderFrontendResourceAccess.h"
#include "RenderResourceOperations.h"
#include "RenderResourceRegistry.h"
#include "RenderResourceUploadQueue.h"

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <utility>

namespace Horo::Render {
    namespace {
        [[nodiscard]] Error MakeFrontendError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] Result<void> ReleaseOrCancelResource(Detail::RenderResourceRegistry &registry,
                                                           const Detail::RenderResourceClass resourceClass,
                                                           const Detail::RenderResourceIdentity identity) {
            const auto state = registry.State(resourceClass, identity);
            if (state.HasError())
                return Result<void>::Failure(state.ErrorValue());
            Result<void> released = state.Value() == RenderResourceState::Pending ? registry.CancelPending(resourceClass, identity)
                                                                                  : registry.Release(resourceClass, identity);
            if (released.HasValue())
                static_cast<void>(registry.DrainRetirements());
            return released;
        }
    }  // namespace

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

    /** @copydoc RenderFrontend::CreateTexture */
    Result<ResourceCreation<RenderTextureHandle>> RenderFrontend::CreateTexture(const RenderTextureDescriptor &descriptor) {
        if (activeFrameScope_ != nullptr) {
            return Result<ResourceCreation<RenderTextureHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A texture cannot be created during an active frame."));
        }
        if (!descriptor.IsValid()) {
            return Result<ResourceCreation<RenderTextureHandle>>::Failure(
                MakeFrontendError(FrontendErrors::InvalidTextureDescriptor, "The texture descriptor is structurally invalid."));
        }
        if (!backend_->Capabilities().supportsTextureResources) {
            return Result<ResourceCreation<RenderTextureHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceUnsupported, "The active renderer backend does not support generic textures."));
        }
        auto reserved = resourceRegistry_->Reserve(Detail::RenderResourceClass::Texture);
        if (reserved.HasError())
            return Result<ResourceCreation<RenderTextureHandle>>::Failure(reserved.ErrorValue());
        const Detail::ResourceReservation reservation = reserved.Value();
        if (reservation.identity.slot >= textures_.size())
            textures_.resize(static_cast<std::size_t>(reservation.identity.slot) + 1);
        textures_[reservation.identity.slot] = {.generation = reservation.identity.generation, .descriptor = descriptor};
        resourceUploadQueue_->EnqueueTexture(reservation.identity, descriptor);
        return Result<ResourceCreation<RenderTextureHandle>>::Success(
            {.handle = TextureHandle(reservation.identity), .operation = reservation.operation});
    }

    /** @copydoc RenderFrontend::CreateTextureView */
    Result<ResourceCreation<RenderTextureViewHandle>> RenderFrontend::CreateTextureView(const RenderTextureViewDescriptor &descriptor) {
        if (activeFrameScope_ != nullptr) {
            return Result<ResourceCreation<RenderTextureViewHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A texture view cannot be created during an active frame."));
        }
        if (!descriptor.IsValid()) {
            return Result<ResourceCreation<RenderTextureViewHandle>>::Failure(
                MakeFrontendError(FrontendErrors::InvalidTextureViewDescriptor, "The texture-view descriptor is structurally invalid."));
        }
        if (!backend_->Capabilities().supportsTextureResources) {
            return Result<ResourceCreation<RenderTextureViewHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceUnsupported,
                                  "The active renderer backend does not support generic texture views."));
        }
        if (const Result<void> dependency = ValidateTextureViewDependency(descriptor); dependency.HasError())
            return Result<ResourceCreation<RenderTextureViewHandle>>::Failure(dependency.ErrorValue());
        const std::array dependencies{Identity(descriptor.texture)};
        auto reserved = resourceRegistry_->Reserve(Detail::RenderResourceClass::TextureView, dependencies);
        if (reserved.HasError())
            return Result<ResourceCreation<RenderTextureViewHandle>>::Failure(reserved.ErrorValue());
        const Detail::ResourceReservation reservation = reserved.Value();
        if (reservation.identity.slot >= textureViews_.size())
            textureViews_.resize(static_cast<std::size_t>(reservation.identity.slot) + 1);
        textureViews_[reservation.identity.slot] = {.generation = reservation.identity.generation, .descriptor = descriptor};
        resourceUploadQueue_->EnqueueTextureView(reservation.identity, descriptor);
        return Result<ResourceCreation<RenderTextureViewHandle>>::Success(
            {.handle = TextureViewHandle(reservation.identity), .operation = reservation.operation});
    }

    /** @copydoc RenderFrontend::CreateRenderTarget */
    Result<ResourceCreation<RenderTargetHandle>> RenderFrontend::CreateRenderTarget(const RenderTargetDescriptor &descriptor) {
        if (activeFrameScope_ != nullptr) {
            return Result<ResourceCreation<RenderTargetHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A render target cannot be created during an active frame."));
        }
        if (!descriptor.IsValid()) {
            return Result<ResourceCreation<RenderTargetHandle>>::Failure(
                MakeFrontendError(FrontendErrors::InvalidRenderTargetDescriptor, "The render-target descriptor is structurally invalid."));
        }
        if (!backend_->Capabilities().supportsRenderTargetResources) {
            return Result<ResourceCreation<RenderTargetHandle>>::Failure(
                MakeFrontendError(FrontendErrors::ResourceUnsupported,
                                  "The active renderer backend does not support generic render targets."));
        }
        if (const Result<void> dependencies = ValidateRenderTargetDependencies(descriptor); dependencies.HasError())
            return Result<ResourceCreation<RenderTargetHandle>>::Failure(dependencies.ErrorValue());
        std::array<Detail::RenderResourceIdentity, 2> dependencies{};
        std::size_t dependencyCount = 0;
        if (descriptor.colorAttachment.IsValid())
            dependencies[dependencyCount++] = Identity(descriptor.colorAttachment);
        if (descriptor.depthAttachment.IsValid())
            dependencies[dependencyCount++] = Identity(descriptor.depthAttachment);
        auto reserved =
            resourceRegistry_->Reserve(Detail::RenderResourceClass::RenderTarget, std::span{dependencies}.first(dependencyCount));
        if (reserved.HasError())
            return Result<ResourceCreation<RenderTargetHandle>>::Failure(reserved.ErrorValue());
        const Detail::ResourceReservation reservation = reserved.Value();
        if (reservation.identity.slot >= targets_.size())
            targets_.resize(static_cast<std::size_t>(reservation.identity.slot) + 1);
        targets_[reservation.identity.slot].extent = descriptor.extent;
        resourceUploadQueue_->EnqueueRenderTarget(reservation.identity, descriptor);
        return Result<ResourceCreation<RenderTargetHandle>>::Success(
            {.handle = TargetHandle(reservation.identity), .operation = reservation.operation});
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

    /** @copydoc RenderFrontend::ResourceState(RenderTextureHandle) */
    Result<RenderResourceState> RenderFrontend::ResourceState(const RenderTextureHandle texture) const {
        return resourceRegistry_->State(Detail::RenderResourceClass::Texture, Identity(texture));
    }

    /** @copydoc RenderFrontend::ResourceState(RenderTextureViewHandle) */
    Result<RenderResourceState> RenderFrontend::ResourceState(const RenderTextureViewHandle view) const {
        return resourceRegistry_->State(Detail::RenderResourceClass::TextureView, Identity(view));
    }

    /** @copydoc RenderFrontend::ResourceState(RenderTargetHandle) */
    Result<RenderResourceState> RenderFrontend::ResourceState(const RenderTargetHandle target) const {
        return resourceRegistry_->State(Detail::RenderResourceClass::RenderTarget, Identity(target));
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
        return ReleaseOrCancelResource(*resourceRegistry_, Detail::RenderResourceClass::Buffer, Identity(buffer));
    }

    /** @copydoc RenderFrontend::ReleaseMesh */
    Result<void> RenderFrontend::ReleaseMesh(const RenderMeshHandle mesh) {
        if (activeFrameScope_ != nullptr) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A mesh cannot be released during an active frame."));
        }
        return ReleaseOrCancelResource(*resourceRegistry_, Detail::RenderResourceClass::Mesh, Identity(mesh));
    }

    /** @copydoc RenderFrontend::ReleaseTexture */
    Result<void> RenderFrontend::ReleaseTexture(const RenderTextureHandle texture) {
        if (activeFrameScope_ != nullptr)
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A texture cannot be released during an active frame."));
        return ReleaseOrCancelResource(*resourceRegistry_, Detail::RenderResourceClass::Texture, Identity(texture));
    }

    /** @copydoc RenderFrontend::ReleaseTextureView */
    Result<void> RenderFrontend::ReleaseTextureView(const RenderTextureViewHandle view) {
        if (activeFrameScope_ != nullptr)
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A texture view cannot be released during an active frame."));
        return ReleaseOrCancelResource(*resourceRegistry_, Detail::RenderResourceClass::TextureView, Identity(view));
    }

    /** @copydoc RenderFrontend::ReleaseRenderTarget */
    Result<void> RenderFrontend::ReleaseRenderTarget(const RenderTargetHandle target) {
        if (activeFrameScope_ != nullptr)
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceChangeDuringFrame, "A render target cannot be released during an active frame."));
        const Result<void> released =
            ReleaseOrCancelResource(*resourceRegistry_, Detail::RenderResourceClass::RenderTarget, Identity(target));
        if (released.HasValue()) {
            if (target.slot < targets_.size())
                targets_[target.slot] = {};
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

    Result<void> RenderFrontend::ValidateTextureViewDependency(const RenderTextureViewDescriptor &descriptor) const {
        const auto textureState = resourceRegistry_->State(Detail::RenderResourceClass::Texture, Identity(descriptor.texture));
        if (textureState.HasError())
            return Result<void>::Failure(textureState.ErrorValue());
        if (textureState.Value() != RenderResourceState::Ready) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceDependencyNotReady, "Texture-view creation requires a ready texture."));
        }
        if (descriptor.texture.slot >= textures_.size()) {
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::InvalidTextureViewDescriptor, "Texture-view source metadata is unavailable."));
        }
        const TextureRecord &texture = textures_[descriptor.texture.slot];
        const bool colorFormat = texture.descriptor.format == RenderTextureFormat::Rgba8Unorm;
        const bool aspectCompatible =
            colorFormat ? descriptor.aspect == RenderTextureAspect::Color : descriptor.aspect != RenderTextureAspect::Color;
        const std::array compatible{
            texture.generation == descriptor.texture.generation,
            descriptor.format == texture.descriptor.format,
            descriptor.baseMip == 0,
            descriptor.mipCount == texture.descriptor.mipCount,
            descriptor.baseLayer == 0,
            descriptor.layerCount == texture.descriptor.layerCount,
            aspectCompatible,
        };
        if (!std::ranges::all_of(compatible, std::identity{})) {
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::InvalidTextureViewDescriptor,
                                                           "Texture-view format, range, or aspect is incompatible with its texture."));
        }
        return Result<void>::Success();
    }

    Result<void> RenderFrontend::ValidateRenderTargetDependencies(const RenderTargetDescriptor &descriptor) const {
        if (const Result<void> color = ValidateRenderTargetAttachment(descriptor.colorAttachment, RenderTextureAspect::Color,
                                                                      descriptor.extent, descriptor.sampleCount);
            color.HasError())
            return color;
        if (const Result<void> depth = ValidateRenderTargetAttachment(descriptor.depthAttachment, RenderTextureAspect::Depth,
                                                                      descriptor.extent, descriptor.sampleCount);
            depth.HasError())
            return depth;
        return Result<void>::Success();
    }

    Result<void> RenderFrontend::ValidateRenderTargetAttachment(const RenderTextureViewHandle handle,
                                                                const RenderTextureAspect requiredAspect, const FramebufferExtent extent,
                                                                const std::uint32_t sampleCount) const {
        if (!handle.IsValid())
            return Result<void>::Success();
        const auto state = resourceRegistry_->State(Detail::RenderResourceClass::TextureView, Identity(handle));
        if (state.HasError())
            return Result<void>::Failure(state.ErrorValue());
        if (state.Value() != RenderResourceState::Ready)
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::ResourceDependencyNotReady, "Render-target creation requires ready attachment views."));
        if (handle.slot >= textureViews_.size())
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::InvalidRenderTargetDescriptor, "Render-target attachment metadata is unavailable."));
        const TextureViewRecord &view = textureViews_[handle.slot];
        const bool aspectCompatible = requiredAspect == RenderTextureAspect::Color ? view.descriptor.aspect == RenderTextureAspect::Color
                                                                                   : view.descriptor.aspect != RenderTextureAspect::Color;
        if (view.generation != handle.generation || !aspectCompatible || view.descriptor.texture.slot >= textures_.size())
            return Result<void>::Failure(
                MakeFrontendError(FrontendErrors::InvalidRenderTargetDescriptor, "Render-target attachment aspect is incompatible."));
        const TextureRecord &texture = textures_[view.descriptor.texture.slot];
        const std::array compatible{
            texture.descriptor.extent == extent,
            texture.descriptor.sampleCount == sampleCount,
            HasTextureUsage(texture.descriptor.usage, RenderTextureUsage::RenderAttachment),
        };
        if (!std::ranges::all_of(compatible, std::identity{}))
            return Result<void>::Failure(MakeFrontendError(FrontendErrors::InvalidRenderTargetDescriptor,
                                                           "Render-target attachment extent, samples, or usage is incompatible."));
        return Result<void>::Success();
    }

    bool RenderFrontend::IsLiveTarget(const RenderTargetHandle target, const FramebufferExtent extent) const noexcept {
        const auto state =
            resourceRegistry_->State(Detail::RenderResourceClass::RenderTarget, {target.owner, target.slot, target.generation});
        return state.HasValue() && state.Value() == RenderResourceState::Ready && target.slot < targets_.size() &&
               targets_[target.slot].extent.width == extent.width && targets_[target.slot].extent.height == extent.height;
    }

    Result<std::uint64_t> RenderFrontend::BackendInstance(const RenderMeshHandle mesh) const {
        return resourceRegistry_->BackendInstance(Detail::RenderResourceClass::Mesh, Identity(mesh));
    }

    Result<std::uint64_t> RenderFrontend::BackendInstance(const RenderTextureViewHandle view) const {
        return resourceRegistry_->BackendInstance(Detail::RenderResourceClass::TextureView, Identity(view));
    }

    Result<std::uint64_t> RenderFrontend::BackendInstance(const RenderTargetHandle target) const {
        return resourceRegistry_->BackendInstance(Detail::RenderResourceClass::RenderTarget, Identity(target));
    }

    Result<std::uint64_t> Detail::RenderFrontendResourceAccess::BackendInstance(const RenderFrontend &frontend,
                                                                                const RenderMeshHandle mesh) {
        return frontend.BackendInstance(mesh);
    }

    Result<std::uint64_t> Detail::RenderFrontendResourceAccess::BackendInstance(const RenderFrontend &frontend,
                                                                                const RenderTextureViewHandle view) {
        return frontend.BackendInstance(view);
    }

    Result<std::uint64_t> Detail::RenderFrontendResourceAccess::BackendInstance(const RenderFrontend &frontend,
                                                                                const RenderTargetHandle target) {
        return frontend.BackendInstance(target);
    }
}  // namespace Horo::Render
