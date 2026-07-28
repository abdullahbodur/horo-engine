#include "Horo/Foundation/Logging/StructuredLogStore.h"

#include <algorithm>
#include <utility>

namespace Horo::Log {
/** @copydoc StructuredLogStore::StructuredLogStore */
StructuredLogStore::StructuredLogStore(const std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1U)) {
}

/** @copydoc StructuredLogStore::Append */
void StructuredLogStore::Append(StructuredLogRecord record) {
    auto immutableRecord = std::make_shared<const StructuredLogRecord>(std::move(record));
    std::lock_guard lock(mutex_);
    if (records_.size() == capacity_) {
        records_.pop_front();
        ++droppedRecordCount_;
    }
    records_.push_back(std::move(immutableRecord));
    ++revision_;
}

/** @copydoc StructuredLogStore::SnapshotIfChanged */
std::optional<StructuredLogSnapshot> StructuredLogStore::SnapshotIfChanged(
    const std::uint64_t knownRevision) const {
    std::lock_guard lock(mutex_);
    if (knownRevision == revision_)
        return std::nullopt;

    return StructuredLogSnapshot{
        .revision = revision_,
        .droppedRecordCount = droppedRecordCount_,
        .capacity = capacity_,
        .records = {records_.begin(), records_.end()},
    };
}
} // namespace Horo::Log
