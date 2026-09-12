#include "Horo/WorldStreaming/StreamingCellAssetRequest.h"

#include "Horo/Assets/AssetProvider.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsKnown(const StreamingCellAssetRequestLifecycle value) noexcept {
            return value < StreamingCellAssetRequestLifecycle::Count;
        }

        [[nodiscard]] const WorldPartitionCellDescriptor *FindCell(const CookedWorldIndexManifest &manifest, const StreamingCellId &cell) {
            const auto cells = manifest.Descriptor().Cells();
            const auto found = std::ranges::lower_bound(cells, cell, StreamingCellCanonicalLess{}, &WorldPartitionCellDescriptor::id);
            return found != cells.end() && found->id == cell ? &*found : nullptr;
        }

        [[nodiscard]] const CookedWorldCellManifestEntry *FindManifestCell(const CookedWorldIndexManifest &manifest,
                                                                           const StreamingCellId &cell) {
            const auto cells = manifest.Cells();
            const auto found = std::ranges::lower_bound(cells, cell, StreamingCellCanonicalLess{}, &CookedWorldCellManifestEntry::cell);
            return found != cells.end() && found->cell == cell ? &*found : nullptr;
        }

        [[nodiscard]] bool Matches(const CookedWorldCellManifestEntry &manifest,
                                   const StreamingCellCandidateManifestRecord &candidate) noexcept {
            return manifest.cell == candidate.cell && manifest.uncompressedSize == candidate.uncompressedSize &&
                   manifest.compressedSize == candidate.compressedSize && manifest.payloadCrc32 == candidate.payloadCrc32 &&
                   manifest.artifactHash == candidate.artifactHash;
        }

        [[nodiscard]] bool IsTerminal(const Assets::AssetLoadState state) noexcept {
            using enum Assets::AssetLoadState;
            return state == Succeeded || state == Failed || state == Cancelled;
        }
    }  // namespace

    struct StreamingCellAssetRequest::StateData final {
        StreamingCellAssetRequestId request;
        StreamingCellOperationHandle operation;
        Assets::AssetRegistryRevision registryRevision;
        CancellationSource cancellation;
        std::vector<Assets::AssetId> assets;
        std::vector<Assets::AssetLoadHandle> handles;
        mutable std::mutex mutex;
        bool cancellationRequested{};
        bool consumed{};

        [[nodiscard]] StreamingCellAssetRequestState RefreshState() noexcept {
            using enum Assets::AssetLoadState;
            bool anyLoading{};
            bool anyFailed{};
            bool anyCancelled{};
            for (const auto &handle : handles) {
                const auto state = handle.State();
                anyLoading = anyLoading || !IsTerminal(state);
                anyFailed = anyFailed || state == Failed;
                anyCancelled = anyCancelled || state == Cancelled;
            }
            if (anyLoading) {
                if (anyFailed || anyCancelled)
                    CancelChildren();
                return cancellationRequested ? StreamingCellAssetRequestState::Cancelling : StreamingCellAssetRequestState::Loading;
            }
            if (anyFailed)
                return StreamingCellAssetRequestState::Failed;
            if (anyCancelled || cancellationRequested)
                return StreamingCellAssetRequestState::Cancelled;
            return StreamingCellAssetRequestState::Ready;
        }

        void CancelChildren() noexcept {
            cancellationRequested = true;
            cancellation.RequestCancellation();
            for (auto &handle : handles)
                static_cast<void>(handle.RequestCancel());
        }
    };

    StreamingCellAssetRequest::StreamingCellAssetRequest(std::shared_ptr<StateData> state) noexcept : state_(std::move(state)) {}

    /** @copydoc StreamingCellAssetRequest::~StreamingCellAssetRequest */
    StreamingCellAssetRequest::~StreamingCellAssetRequest() {
        if (state_)
            static_cast<void>(RequestCancel());
    }

    /** @copydoc StreamingCellAssetRequest::operator= */
    StreamingCellAssetRequest &StreamingCellAssetRequest::operator=(StreamingCellAssetRequest &&other) noexcept {
        if (this == &other)
            return *this;
        if (state_)
            static_cast<void>(RequestCancel());
        state_ = std::move(other.state_);
        return *this;
    }

    /** @copydoc StreamingCellAssetRequest::State */
    StreamingCellAssetRequestState StreamingCellAssetRequest::State() const noexcept {
        if (!state_)
            return StreamingCellAssetRequestState::Failed;
        std::scoped_lock lock{state_->mutex};
        return state_->RefreshState();
    }

    /** @copydoc StreamingCellAssetRequest::RequestCancel */
    Result<void> StreamingCellAssetRequest::RequestCancel() noexcept {
        if (!state_)
            return Failure<void>(WorldStreamingErrors::CellAssetRequestLifecycleUnavailable);
        std::scoped_lock lock{state_->mutex};
        state_->CancelChildren();
        return Result<void>::Success();
    }

    /** @copydoc StreamingCellAssetRequest::TakeResult */
    Result<StreamingCellAssetBatch> StreamingCellAssetRequest::TakeResult() {
        if (!state_)
            return Failure<StreamingCellAssetBatch>(WorldStreamingErrors::CellAssetRequestLifecycleUnavailable);
        std::scoped_lock lock{state_->mutex};
        const auto state = state_->RefreshState();
        if (state == StreamingCellAssetRequestState::Loading || state == StreamingCellAssetRequestState::Cancelling)
            return Failure<StreamingCellAssetBatch>(WorldStreamingErrors::CellAssetRequestNotReady);
        if (state_->consumed)
            return Failure<StreamingCellAssetBatch>(WorldStreamingErrors::CellAssetRequestConsumed);
        state_->consumed = true;

        StreamingCellAssetBatch batch{state_->request, state_->operation, state_->registryRevision, {}};
        batch.assets.reserve(state_->handles.size());
        for (std::size_t index{}; index < state_->handles.size(); ++index) {
            auto loaded = state_->handles[index].TakeResult();
            if (loaded.HasError())
                return Result<StreamingCellAssetBatch>::Failure(loaded.ErrorValue());
            auto value = std::move(loaded).Value();
            batch.assets.push_back({state_->assets[index], std::move(value.bytes)});
        }
        return Result<StreamingCellAssetBatch>::Success(std::move(batch));
    }

    /** @copydoc RequestStreamingCellAssets */
    Result<StreamingCellAssetRequest> RequestStreamingCellAssets(Assets::AssetLoadService &service,
                                                                 const Assets::AssetRegistrySnapshot &registry,
                                                                 const CookedWorldIndexManifest &manifest,
                                                                 const StreamingCellCandidate &candidate,
                                                                 const StreamingCellAssetRequestContext &context) {
        if (!IsKnown(context.lifecycle) || !context.request.IsValid() || !context.operation.IsValid() || context.maximumRequests == 0)
            return Failure<StreamingCellAssetRequest>(WorldStreamingErrors::CellAssetRequestInvalid);
        if (context.lifecycle != StreamingCellAssetRequestLifecycle::Active)
            return Failure<StreamingCellAssetRequest>(WorldStreamingErrors::CellAssetRequestLifecycleUnavailable);
        if (context.operation != candidate.Operation() || context.operation.fence.partition != manifest.Descriptor().Partition())
            return Failure<StreamingCellAssetRequest>(WorldStreamingErrors::CellAssetRequestStale);
        const auto *manifestCell = FindManifestCell(manifest, context.operation.fence.cell);
        const auto *descriptorCell = FindCell(manifest, context.operation.fence.cell);
        if (!manifestCell || !descriptorCell || !Matches(*manifestCell, candidate.ManifestEntry()) ||
            descriptorCell->package.chunkAsset != candidate.ChunkAsset())
            return Failure<StreamingCellAssetRequest>(WorldStreamingErrors::CellAssetRequestStale);
        if (candidate.HardDependencies().size() >= context.maximumRequests)
            return Failure<StreamingCellAssetRequest>(WorldStreamingErrors::CellAssetRequestCapacityExceeded);

        auto state = std::make_shared<StreamingCellAssetRequest::StateData>();
        state->request = context.request;
        state->operation = context.operation;
        state->registryRevision = registry.Revision();
        state->assets.reserve(candidate.HardDependencies().size() + 1U);
        state->handles.reserve(candidate.HardDependencies().size() + 1U);
        state->assets.push_back(candidate.ChunkAsset());
        for (const auto &dependency : candidate.HardDependencies()) {
            const auto *cell = FindCell(manifest, dependency);
            if (!cell)
                return Failure<StreamingCellAssetRequest>(WorldStreamingErrors::CellAssetRequestUnavailable);
            state->assets.push_back(cell->package.chunkAsset);
        }
        for (const auto &asset : state->assets)
            if (!registry.Find(asset))
                return Failure<StreamingCellAssetRequest>(WorldStreamingErrors::CellAssetRequestUnavailable);

        const auto cancellation = state->cancellation.Token();
        for (const auto &asset : state->assets) {
            auto submitted = service.LoadAsync(registry, asset, cancellation);
            if (submitted.HasError()) {
                state->CancelChildren();
                return Result<StreamingCellAssetRequest>::Failure(submitted.ErrorValue());
            }
            state->handles.push_back(std::move(submitted).Value());
        }
        return Result<StreamingCellAssetRequest>::Success(StreamingCellAssetRequest{std::move(state)});
    }
}  // namespace Horo::WorldStreaming
