#pragma once

#include "Horo/Runtime/Render/RenderFrontend.h"
#include "RenderResourceRegistry.h"
#include "RenderResourceUploadQueue.h"

namespace Horo::Render {
    [[nodiscard]] Detail::RenderResourceIdentity Identity(RenderBufferHandle handle) noexcept;
    [[nodiscard]] Detail::RenderResourceIdentity Identity(RenderMeshHandle handle) noexcept;
    [[nodiscard]] RenderBufferHandle BufferHandle(Detail::RenderResourceIdentity identity) noexcept;
    [[nodiscard]] RenderMeshHandle MeshHandle(Detail::RenderResourceIdentity identity) noexcept;
    [[nodiscard]] bool FitsBuffer(std::uint32_t elementSize, std::uint32_t elementCount, std::size_t bufferSize) noexcept;
    [[nodiscard]] Result<std::uint64_t> RealizeResourceRequest(IRenderBackend &backend, Detail::RenderResourceRegistry &registry,
                                                               const Detail::RenderResourceUploadQueue::Request &request);
    void CompleteResourceRequest(IRenderBackend &backend, Detail::RenderResourceRegistry &registry,
                                 const Detail::RenderResourceUploadQueue::Request &request, const Result<std::uint64_t> &created);
}  // namespace Horo::Render
