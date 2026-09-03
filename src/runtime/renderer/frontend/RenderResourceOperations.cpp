#include "RenderResourceOperations.h"

#include "RenderFrontendErrors.h"

#include <limits>
#include <string>
#include <utility>

namespace Horo::Render {
    namespace {
        using UploadRequest = Detail::RenderResourceUploadQueue::Request;
        using UploadRequestKind = Detail::RenderResourceUploadQueue::RequestKind;

        [[nodiscard]] Detail::RenderResourceClass ResourceClassFor(const UploadRequestKind kind) noexcept {
            return kind == UploadRequestKind::Buffer ? Detail::RenderResourceClass::Buffer : Detail::RenderResourceClass::Mesh;
        }

        [[nodiscard]] Result<std::uint64_t> RealizeMeshRequest(IRenderBackend &backend, const Detail::RenderResourceRegistry &registry,
                                                               const RenderMeshDescriptor &descriptor) {
            const auto vertex = registry.BackendInstance(Detail::RenderResourceClass::Buffer, Identity(descriptor.vertexBuffer));
            if (vertex.HasError()) {
                return Result<std::uint64_t>::Failure(vertex.ErrorValue());
            }
            const auto index = registry.BackendInstance(Detail::RenderResourceClass::Buffer, Identity(descriptor.indexBuffer));
            if (index.HasError()) {
                return Result<std::uint64_t>::Failure(index.ErrorValue());
            }
            return backend.CreateMesh(descriptor, vertex.Value(), index.Value());
        }

        void DestroyResourceInstance(IRenderBackend &backend, const Detail::RenderResourceClass resourceClass,
                                     const std::uint64_t backendInstance) noexcept {
            if (resourceClass == Detail::RenderResourceClass::Buffer) {
                backend.DestroyBuffer(backendInstance);
                return;
            }
            backend.DestroyMesh(backendInstance);
        }
    }  // namespace

    Detail::RenderResourceIdentity Identity(const RenderBufferHandle handle) noexcept {
        return {handle.owner, handle.slot, handle.generation};
    }

    Detail::RenderResourceIdentity Identity(const RenderMeshHandle handle) noexcept {
        return {handle.owner, handle.slot, handle.generation};
    }

    RenderBufferHandle BufferHandle(const Detail::RenderResourceIdentity identity) noexcept {
        return {identity.owner, identity.slot, identity.generation};
    }

    RenderMeshHandle MeshHandle(const Detail::RenderResourceIdentity identity) noexcept {
        return {identity.owner, identity.slot, identity.generation};
    }

    bool FitsBuffer(const std::uint32_t elementSize, const std::uint32_t elementCount, const std::size_t bufferSize) noexcept {
        return elementSize != 0 && elementCount <= std::numeric_limits<std::size_t>::max() / elementSize &&
               static_cast<std::size_t>(elementSize) * elementCount <= bufferSize;
    }

    Result<std::uint64_t> RealizeResourceRequest(IRenderBackend &backend, const Detail::RenderResourceRegistry &registry,
                                                 const UploadRequest &request) {
        try {
            if (request.kind == UploadRequestKind::Buffer) {
                return backend.CreateBuffer(request.buffer, request.initialData);
            }
            return RealizeMeshRequest(backend, registry, request.mesh);
        } catch (...) {  // NOSONAR(cpp:S2738)
            return Result<std::uint64_t>::Failure(
                MakeError(FrontendErrors::ResourceBackendException, "Renderer backend resource realization threw an exception."));
        }
    }

    void CompleteResourceRequest(IRenderBackend &backend, Detail::RenderResourceRegistry &registry, const UploadRequest &request,
                                 const Result<std::uint64_t> &created) {
        const Detail::RenderResourceClass resourceClass = ResourceClassFor(request.kind);
        if (created.HasError()) {
            static_cast<void>(registry.Fail(resourceClass, request.identity, created.ErrorValue()));
            return;
        }
        if (const Result<void> published = registry.Publish(resourceClass, request.identity, created.Value()); published.HasError()) {
            DestroyResourceInstance(backend, resourceClass, created.Value());
            static_cast<void>(registry.Fail(resourceClass, request.identity, published.ErrorValue()));
            return;
        }
        if (request.replacedMesh.has_value()) {
            static_cast<void>(registry.Release(Detail::RenderResourceClass::Mesh, *request.replacedMesh));
        }
    }
}  // namespace Horo::Render
