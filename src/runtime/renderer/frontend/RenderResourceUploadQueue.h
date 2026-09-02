#pragma once

#include "Horo/Runtime/Render/Mesh.h"
#include "Horo/Runtime/Render/RenderFrontend.h"
#include "RenderResourceRegistry.h"

#include <deque>
#include <optional>
#include <vector>

namespace Horo::Render::Detail {
    class RenderResourceUploadQueue final {
    public:
        enum class RequestKind : std::uint8_t {
            Buffer,
            Mesh,
        };

        struct Request {
            RequestKind kind{RequestKind::Buffer};
            RenderResourceIdentity identity;
            RenderBufferDescriptor buffer;
            RenderMeshDescriptor mesh;
            std::vector<std::byte> initialData;
            std::optional<RenderResourceIdentity> replacedMesh;
        };

        explicit RenderResourceUploadQueue(RenderResourceUploadLimits limits) : limits_(limits) {}

        [[nodiscard]] bool CanEnqueue(const std::size_t byteCount) const noexcept {
            return byteCount <= limits_.maximumBytesPerDrain && byteCount <= limits_.maximumPendingBytes - pendingBytes_;
        }

        void EnqueueBuffer(const RenderResourceIdentity identity, const RenderBufferDescriptor &descriptor,
                           const std::span<const std::byte> initialData) {
            Request request{.kind = RequestKind::Buffer, .identity = identity, .buffer = descriptor};
            request.initialData.assign(initialData.begin(), initialData.end());
            pendingBytes_ += request.initialData.size();
            requests_.push_back(std::move(request));
        }

        void EnqueueMesh(const RenderResourceIdentity identity, const RenderMeshDescriptor &descriptor,
                         const std::optional<RenderResourceIdentity> replacedMesh) {
            requests_.push_back(Request{.kind = RequestKind::Mesh, .identity = identity, .mesh = descriptor, .replacedMesh = replacedMesh});
        }

        void MarkBackAsReplacement(const RenderResourceIdentity replacedMesh) noexcept {
            requests_.back().replacedMesh = replacedMesh;
        }

        [[nodiscard]] bool Empty() const noexcept {
            return requests_.empty();
        }

        [[nodiscard]] const Request &Front() const noexcept {
            return requests_.front();
        }

        [[nodiscard]] bool DrainLimitReached(const std::size_t completedRequests, const std::size_t completedBytes) const noexcept {
            if (completedRequests >= limits_.maximumRequestsPerDrain) {
                return true;
            }
            return completedRequests > 0 && completedBytes + requests_.front().initialData.size() > limits_.maximumBytesPerDrain;
        }

        Request Pop() {
            Request request = std::move(requests_.front());
            requests_.pop_front();
            pendingBytes_ -= request.initialData.size();
            return request;
        }

        void Clear() noexcept {
            requests_.clear();
            pendingBytes_ = 0;
        }

    private:
        RenderResourceUploadLimits limits_;
        std::deque<Request> requests_;
        std::size_t pendingBytes_{0};
    };
}  // namespace Horo::Render::Detail
