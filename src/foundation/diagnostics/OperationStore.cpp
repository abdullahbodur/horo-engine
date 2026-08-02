#include "Horo/Foundation/OperationStore.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace Horo {
    namespace {
        [[nodiscard]] bool IsTerminal(const OperationState state) noexcept {
            return state == OperationState::Succeeded || state == OperationState::Failed || state == OperationState::Cancelled;
        }

        [[nodiscard]] bool IsTransitionAllowed(const OperationState from, const OperationState to) noexcept {
            if (IsTerminal(from))
                return false;
            switch (from) {
                case OperationState::Queued:
                    return to == OperationState::Queued || to == OperationState::Running || to == OperationState::Cancelling ||
                           IsTerminal(to);
                case OperationState::Running:
                    return to == OperationState::Running || to == OperationState::Waiting || to == OperationState::Cancelling ||
                           IsTerminal(to);
                case OperationState::Waiting:
                    return to == OperationState::Waiting || to == OperationState::Running || to == OperationState::Cancelling ||
                           IsTerminal(to);
                case OperationState::Cancelling:
                    return to == OperationState::Cancelling || IsTerminal(to);
                case OperationState::Succeeded:
                case OperationState::Failed:
                case OperationState::Cancelled:
                    return false;
            }
            return false;
        }
    }  // namespace

    /** @copydoc OperationStore::OperationStore */
    OperationStore::OperationStore(const std::size_t activeCapacity, const std::size_t recentCapacity)
        : activeCapacity_(std::max<std::size_t>(activeCapacity, 1U)), recentCapacity_(std::max<std::size_t>(recentCapacity, 1U)) {}

    /** @copydoc OperationStore::Begin */
    std::optional<OperationId> OperationStore::Begin(OperationDescriptor descriptor) {
        std::lock_guard lock(mutex_);
        if (active_.size() >= activeCapacity_)
            return std::nullopt;

        const OperationId id = nextId_++;
        const auto now = std::chrono::steady_clock::now();
        const std::optional<float> progress =
            descriptor.progress.has_value() ? std::optional<float>{std::clamp(*descriptor.progress, 0.0F, 1.0F)} : std::nullopt;
        active_.emplace(id,
                        ActiveOperation{
                            .record = OperationRecord{.id = id,
                                                      .kind = descriptor.kind,
                                                      .state = OperationState::Queued,
                                                      .title = std::move(descriptor.title),
                                                      .phase = std::move(descriptor.phase),
                                                      .message = std::move(descriptor.message),
                                                      .progress = progress,
                                                      .startedAt = now,
                                                      .cancellable = descriptor.cancellable && static_cast<bool>(descriptor.requestCancel)},
                            .requestCancel = std::move(descriptor.requestCancel),
                        });
        ++revision_;
        return id;
    }

    /** @copydoc OperationStore::Update */
    bool OperationStore::Update(const OperationId id, OperationUpdate update) {
        std::lock_guard lock(mutex_);
        const auto found = active_.find(id);
        if (found == active_.end() || !IsTransitionAllowed(found->second.record.state, update.state))
            return false;

        OperationRecord &record = found->second.record;
        if (update.progress.has_value()) {
            const float bounded = std::clamp(*update.progress, 0.0F, 1.0F);
            const std::string_view effectivePhase = update.phase.empty() ? std::string_view{record.phase} : std::string_view{update.phase};
            if (effectivePhase == record.phase && record.progress.has_value() && bounded < *record.progress)
                return false;
            record.progress = bounded;
        }
        if (!update.phase.empty())
            record.phase = std::move(update.phase);
        record.message = std::move(update.message);
        record.error = std::move(update.error);
        record.state = update.state;

        if (IsTerminal(record.state)) {
            record.finishedAt = std::chrono::steady_clock::now();
            recent_.push_back(std::move(record));
            active_.erase(found);
            if (recent_.size() > recentCapacity_) {
                recent_.pop_front();
                ++droppedTerminalCount_;
            }
        }
        ++revision_;
        return true;
    }

    /** @copydoc OperationStore::RequestCancel */
    bool OperationStore::RequestCancel(const OperationId id) {
        std::function<void()> callback;
        {
            std::lock_guard lock(mutex_);
            const auto found = active_.find(id);
            if (found == active_.end() || !found->second.record.cancellable || found->second.cancellationDispatched)
                return false;
            found->second.cancellationDispatched = true;
            found->second.record.state = OperationState::Cancelling;
            callback = found->second.requestCancel;
            ++revision_;
        }
        callback();
        return true;
    }

    /** @copydoc OperationStore::SnapshotIfChanged */
    std::optional<OperationStoreSnapshot> OperationStore::SnapshotIfChanged(const std::uint64_t knownRevision) const {
        std::lock_guard lock(mutex_);
        if (knownRevision == revision_)
            return std::nullopt;

        OperationStoreSnapshot snapshot{.revision = revision_,
                                        .activeCapacity = activeCapacity_,
                                        .recentCapacity = recentCapacity_,
                                        .droppedTerminalCount = droppedTerminalCount_};
        snapshot.operations.reserve(active_.size() + recent_.size());
        for (const auto &[id, operation] : active_) {
            static_cast<void>(id);
            snapshot.operations.push_back(operation.record);
        }
        snapshot.operations.insert(snapshot.operations.end(), recent_.begin(), recent_.end());
        std::ranges::sort(snapshot.operations, {}, &OperationRecord::id);
        return snapshot;
    }
}  // namespace Horo
