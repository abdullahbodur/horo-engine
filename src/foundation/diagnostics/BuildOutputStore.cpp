#include "Horo/Foundation/BuildOutputStore.h"

#include <algorithm>
#include <utility>

namespace Horo {
    /** @copydoc BuildOutputStore::BuildOutputStore */
    BuildOutputStore::BuildOutputStore(const std::size_t capacity) : capacity_(std::max<std::size_t>(capacity, 1U)) {}

    /** @copydoc BuildOutputStore::Append */
    void BuildOutputStore::Append(BuildOutputRecord record) {
        std::lock_guard lock(mutex_);
        record.sequence = nextSequence_++;
        if (records_.size() == capacity_) {
            records_.pop_front();
            ++droppedRecordCount_;
        }
        records_.push_back(std::move(record));
        ++revision_;
    }

    /** @copydoc BuildOutputStore::SnapshotIfChanged */
    std::optional<BuildOutputSnapshot> BuildOutputStore::SnapshotIfChanged(const std::uint64_t knownRevision) const {
        std::lock_guard lock(mutex_);
        if (knownRevision == revision_)
            return std::nullopt;
        return BuildOutputSnapshot{.revision = revision_,
                                   .droppedRecordCount = droppedRecordCount_,
                                   .capacity = capacity_,
                                   .records = {records_.begin(), records_.end()}};
    }
}  // namespace Horo
