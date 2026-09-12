#include "Horo/Network/TransportBudget.h"

#include "Horo/Network/NetworkErrors.h"

#include <limits>
#include <new>
#include <utility>

namespace Horo::Network {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Fail(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool AddFits(const std::size_t current, const std::size_t added, const std::size_t maximum) noexcept {
            return added <= maximum && current <= maximum - added;
        }

        [[nodiscard]] bool ValidatePolicy(const TransportBudgetCapacity &capacity, const TransportLimitPolicyV1 &policy) noexcept {
            if (capacity.maximumConnections == 0 || capacity.maximumQueuedMessages == 0 || capacity.maximumQueuedBytes == 0 ||
                capacity.maximumConnections > std::numeric_limits<std::uint32_t>::max() ||
                capacity.maximumQueuedMessages > std::numeric_limits<std::uint32_t>::max())
                return false;
            if (policy.contractVersion != 1 || policy.revision == 0 || policy.maximumActiveConnections == 0 ||
                policy.maximumQueuedMessages == 0 || policy.maximumQueuedBytes == 0 || policy.maximumQueuedMessagesPerConnection == 0 ||
                policy.maximumQueuedBytesPerConnection == 0 || policy.maximumMessagesPerTick == 0 || policy.maximumBytesPerTick == 0 ||
                policy.maximumMessagesPerConnectionPerTick == 0 || policy.maximumBytesPerConnectionPerTick == 0 ||
                policy.saturationGraceTicks == 0)
                return false;
            return policy.maximumActiveConnections <= capacity.maximumConnections &&
                   policy.maximumQueuedMessages <= capacity.maximumQueuedMessages &&
                   policy.maximumQueuedBytes <= capacity.maximumQueuedBytes &&
                   policy.maximumQueuedMessagesPerConnection <= policy.maximumQueuedMessages &&
                   policy.maximumQueuedBytesPerConnection <= policy.maximumQueuedBytes &&
                   policy.maximumMessagesPerConnectionPerTick <= policy.maximumMessagesPerTick &&
                   policy.maximumBytesPerConnectionPerTick <= policy.maximumBytesPerTick;
        }

        [[nodiscard]] Result<void> ValidateAdmissionState(const TransportAdmissionState state) {
            using enum TransportAdmissionState;
            if (state == Cancelled)
                return Fail<void>(NetworkErrors::TransportOperationCancelled);
            if (state == ShuttingDown)
                return Fail<void>(NetworkErrors::TransportShuttingDown);
            if (state != Accepting)
                return Fail<void>(NetworkErrors::TransportBudgetInvalid);
            return Result<void>::Success();
        }
    }  // namespace

    TransportBudgetController::TransportBudgetController(TransportBudgetCapacity capacity, TransportLimitPolicyV1 policy,
                                                         std::vector<ConnectionEntry> connections, std::vector<QueueEntry> queue,
                                                         std::vector<std::uint32_t> freeSlots) noexcept
        : capacity_(capacity), policy_(policy), connections_(std::move(connections)), queue_(std::move(queue)),
          freeSlots_(std::move(freeSlots)), freeCount_(freeSlots_.size()) {}

    /** @copydoc TransportBudgetController::Create */
    Result<TransportBudgetController> TransportBudgetController::Create(const TransportBudgetCapacity &capacity,
                                                                        const TransportLimitPolicyV1 &policy) {
        if (!ValidatePolicy(capacity, policy))
            return Fail<TransportBudgetController>(NetworkErrors::TransportBudgetInvalid);
        try {
            std::vector<ConnectionEntry> connections(capacity.maximumConnections);
            std::vector<QueueEntry> queue(capacity.maximumQueuedMessages);
            std::vector<std::uint32_t> freeSlots(capacity.maximumQueuedMessages);
            for (std::size_t index = 0; index < freeSlots.size(); ++index)
                freeSlots[index] = static_cast<std::uint32_t>(freeSlots.size() - index - 1);
            return Result<TransportBudgetController>::Success(
                TransportBudgetController{capacity, policy, std::move(connections), std::move(queue), std::move(freeSlots)});
        } catch (const std::bad_alloc &) {
            return Fail<TransportBudgetController>(NetworkErrors::TransportBudgetCapacityExceeded);
        }
    }

    bool TransportBudgetController::PolicyFitsUsage(const TransportLimitPolicyV1 &candidate) const noexcept {
        if (activeConnections_ > candidate.maximumActiveConnections || queuedMessages_ > candidate.maximumQueuedMessages ||
            queuedBytes_ > candidate.maximumQueuedBytes || tickMessages_ > candidate.maximumMessagesPerTick ||
            tickBytes_ > candidate.maximumBytesPerTick)
            return false;
        for (const auto &connection : connections_) {
            if (!connection.active)
                continue;
            if (connection.queuedMessages > candidate.maximumQueuedMessagesPerConnection ||
                connection.queuedBytes > candidate.maximumQueuedBytesPerConnection ||
                connection.tickMessages > candidate.maximumMessagesPerConnectionPerTick ||
                connection.tickBytes > candidate.maximumBytesPerConnectionPerTick)
                return false;
        }
        return true;
    }

    /** @copydoc TransportBudgetController::ReplacePolicy */
    Result<void> TransportBudgetController::ReplacePolicy(const std::uint64_t expectedRevision, const TransportLimitPolicyV1 &candidate,
                                                          const TransportAdmissionState state) {
        if (const auto admission = ValidateAdmissionState(state); admission.HasError())
            return admission;
        if (shuttingDown_)
            return Fail<void>(NetworkErrors::TransportShuttingDown);
        if (expectedRevision != policy_.revision)
            return Fail<void>(NetworkErrors::TransportBudgetPolicyStale);
        if (!ValidatePolicy(capacity_, candidate) || candidate.revision <= policy_.revision)
            return Fail<void>(NetworkErrors::TransportBudgetInvalid);
        if (!PolicyFitsUsage(candidate))
            return Fail<void>(NetworkErrors::TransportBudgetCapacityExceeded);
        policy_ = candidate;
        return Result<void>::Success();
    }

    /** @copydoc TransportBudgetController::BeginTick */
    Result<void> TransportBudgetController::BeginTick(const std::uint64_t tick, const TransportAdmissionState state) {
        if (const auto admission = ValidateAdmissionState(state); admission.HasError())
            return admission;
        if (shuttingDown_)
            return Fail<void>(NetworkErrors::TransportShuttingDown);
        if (tick == 0 || tick <= tick_)
            return Fail<void>(NetworkErrors::TransportBudgetInvalid);
        tick_ = tick;
        tickMessages_ = 0;
        tickBytes_ = 0;
        for (auto &connection : connections_) {
            connection.tickMessages = 0;
            connection.tickBytes = 0;
        }
        return Result<void>::Success();
    }

    TransportBudgetController::ConnectionEntry *TransportBudgetController::FindConnection(const ConnectionHandle connection) noexcept {
        const std::size_t slot = connection.Slot();
        if (slot >= connections_.size())
            return nullptr;
        auto &entry = connections_[slot];
        return entry.active && entry.handle == connection ? &entry : nullptr;
    }

    /** @copydoc TransportBudgetController::OpenConnection */
    Result<void> TransportBudgetController::OpenConnection(const ConnectionHandle connection, const TransportAdmissionState state) {
        if (const auto admission = ValidateAdmissionState(state); admission.HasError())
            return admission;
        if (shuttingDown_)
            return Fail<void>(NetworkErrors::TransportShuttingDown);
        if (!connection.IsValid() || connection.Slot() >= connections_.size())
            return Fail<void>(NetworkErrors::TransportHandleInvalid);
        auto &entry = connections_[connection.Slot()];
        if (entry.active)
            return Fail<void>(entry.handle == connection ? NetworkErrors::TransportBudgetInvalid
                                                         : NetworkErrors::NetworkLifecycleOperationStale);
        if (activeConnections_ >= policy_.maximumActiveConnections)
            return Fail<void>(NetworkErrors::TransportBudgetCapacityExceeded);
        if (entry.initialized) {
            const auto expected = entry.handle.NextGeneration();
            if (expected.HasError() || expected.Value() != connection)
                return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
        }
        entry = ConnectionEntry{.handle = connection, .initialized = true, .active = true};
        ++activeConnections_;
        return Result<void>::Success();
    }

    void TransportBudgetController::Release(QueueEntry &entry) noexcept {
        auto *connection = FindConnection(entry.connection);
        if (connection != nullptr) {
            --connection->queuedMessages;
            connection->queuedBytes -= entry.bytes;
        }
        --queuedMessages_;
        queuedBytes_ -= entry.bytes;
        entry.occupied = false;
        if (entry.generation == std::numeric_limits<std::uint32_t>::max())
            return;
        ++entry.generation;
        freeSlots_[freeCount_++] = static_cast<std::uint32_t>(&entry - queue_.data());
    }

    /** @copydoc TransportBudgetController::CloseConnection */
    Result<std::size_t> TransportBudgetController::CloseConnection(const ConnectionHandle connection) {
        if (!connection.IsValid() || connection.Slot() >= connections_.size())
            return Fail<std::size_t>(NetworkErrors::NetworkLifecycleOperationStale);
        auto &owned = connections_[connection.Slot()];
        if (!owned.initialized || owned.handle != connection)
            return Fail<std::size_t>(NetworkErrors::NetworkLifecycleOperationStale);
        if (!owned.active)
            return Result<std::size_t>::Success(0);
        std::size_t discarded{};
        for (auto &record : queue_) {
            if (record.occupied && record.connection == connection) {
                Release(record);
                ++discarded;
            }
        }
        owned.active = false;
        owned.saturationTicks = 0;
        owned.lastSaturationTick = 0;
        --activeConnections_;
        return Result<std::size_t>::Success(discarded);
    }

    TransportBudgetController::QueueEntry *TransportBudgetController::FindReplaceable(
        const TransportBudgetSubmission &submission) noexcept {
        for (auto &entry : queue_) {
            if (entry.occupied && entry.traffic == TransportTrafficClass::ReplaceableState && entry.connection == submission.connection &&
                entry.replaceableKey == submission.replaceableKey)
                return &entry;
        }
        return nullptr;
    }

    bool TransportBudgetController::FitsNew(const ConnectionEntry &connection, const std::size_t bytes) const noexcept {
        return freeCount_ != 0 && queuedMessages_ < policy_.maximumQueuedMessages &&
               connection.queuedMessages < policy_.maximumQueuedMessagesPerConnection &&
               AddFits(queuedBytes_, bytes, policy_.maximumQueuedBytes) &&
               AddFits(connection.queuedBytes, bytes, policy_.maximumQueuedBytesPerConnection) &&
               tickMessages_ < policy_.maximumMessagesPerTick && connection.tickMessages < policy_.maximumMessagesPerConnectionPerTick &&
               AddFits(tickBytes_, bytes, policy_.maximumBytesPerTick) &&
               AddFits(connection.tickBytes, bytes, policy_.maximumBytesPerConnectionPerTick);
    }

    bool TransportBudgetController::FitsReplacement(const ConnectionEntry &connection, const QueueEntry &record,
                                                    const std::size_t bytes) const noexcept {
        const std::size_t globalWithoutRecord = queuedBytes_ - record.bytes;
        const std::size_t connectionWithoutRecord = connection.queuedBytes - record.bytes;
        return AddFits(globalWithoutRecord, bytes, policy_.maximumQueuedBytes) &&
               AddFits(connectionWithoutRecord, bytes, policy_.maximumQueuedBytesPerConnection) &&
               tickMessages_ < policy_.maximumMessagesPerTick && connection.tickMessages < policy_.maximumMessagesPerConnectionPerTick &&
               AddFits(tickBytes_, bytes, policy_.maximumBytesPerTick) &&
               AddFits(connection.tickBytes, bytes, policy_.maximumBytesPerConnectionPerTick);
    }

    TransportBudgetDecision TransportBudgetController::Overload(ConnectionEntry &connection, const TransportTrafficClass traffic) noexcept {
        if (connection.lastSaturationTick != tick_) {
            connection.lastSaturationTick = tick_;
            if (connection.saturationTicks != std::numeric_limits<std::uint32_t>::max())
                ++connection.saturationTicks;
        }
        if (connection.saturationTicks >= policy_.saturationGraceTicks)
            return {TransportBudgetAdmission::ConnectionMustClose, {}, 0};
        return {traffic == TransportTrafficClass::ReplaceableState ? TransportBudgetAdmission::DroppedReplaceable
                                                                   : TransportBudgetAdmission::Count,
                {},
                0};
    }

    TransportQueueTicket TransportBudgetController::AllocateTicket() {
        if (freeCount_ == 0)
            return {};
        const std::uint32_t slot = freeSlots_[--freeCount_];
        return TransportQueueTicket{slot, queue_[slot].generation};
    }

    /** @copydoc TransportBudgetController::Admit */
    Result<TransportBudgetDecision> TransportBudgetController::Admit(const TransportBudgetSubmission &submission,
                                                                     const TransportAdmissionState state) {
        if (const auto admission = ValidateAdmissionState(state); admission.HasError())
            return Result<TransportBudgetDecision>::Failure(admission.ErrorValue());
        if (shuttingDown_)
            return Fail<TransportBudgetDecision>(NetworkErrors::TransportShuttingDown);
        if (tick_ == 0 || !submission.connection.IsValid() || submission.traffic >= TransportTrafficClass::Count || submission.bytes == 0 ||
            (submission.traffic == TransportTrafficClass::ReplaceableState) != (submission.replaceableKey != 0))
            return Fail<TransportBudgetDecision>(NetworkErrors::TransportBudgetInvalid);
        auto *connection = FindConnection(submission.connection);
        if (connection == nullptr)
            return Fail<TransportBudgetDecision>(NetworkErrors::NetworkLifecycleOperationStale);
        if (submission.bytes > policy_.maximumQueuedBytes || submission.bytes > policy_.maximumQueuedBytesPerConnection ||
            submission.bytes > policy_.maximumBytesPerTick || submission.bytes > policy_.maximumBytesPerConnectionPerTick)
            return Fail<TransportBudgetDecision>(NetworkErrors::TransportBudgetCapacityExceeded);

        if (submission.traffic == TransportTrafficClass::ReplaceableState) {
            if (auto *record = FindReplaceable(submission); record != nullptr) {
                if (!FitsReplacement(*connection, *record, submission.bytes))
                    return Result<TransportBudgetDecision>::Success(Overload(*connection, submission.traffic));
                const std::size_t previousBytes = record->bytes;
                queuedBytes_ = queuedBytes_ - previousBytes + submission.bytes;
                connection->queuedBytes = connection->queuedBytes - previousBytes + submission.bytes;
                record->bytes = submission.bytes;
                ++tickMessages_;
                tickBytes_ += submission.bytes;
                ++connection->tickMessages;
                connection->tickBytes += submission.bytes;
                connection->saturationTicks = 0;
                return Result<TransportBudgetDecision>::Success(
                    {TransportBudgetAdmission::Replaced,
                     TransportQueueTicket{static_cast<std::uint32_t>(record - queue_.data()), record->generation}, previousBytes});
            }
        }

        if (!FitsNew(*connection, submission.bytes)) {
            const auto overload = Overload(*connection, submission.traffic);
            if (submission.traffic == TransportTrafficClass::Reliable &&
                overload.admission != TransportBudgetAdmission::ConnectionMustClose)
                return Fail<TransportBudgetDecision>(NetworkErrors::TransportReliableBackpressure);
            return Result<TransportBudgetDecision>::Success(overload);
        }

        const TransportQueueTicket ticket = AllocateTicket();
        if (!ticket.IsValid())
            return Fail<TransportBudgetDecision>(NetworkErrors::TransportBudgetCapacityExceeded);
        auto &record = queue_[ticket.Slot()];
        record.connection = submission.connection;
        record.traffic = submission.traffic;
        record.replaceableKey = submission.replaceableKey;
        record.bytes = submission.bytes;
        record.occupied = true;
        ++queuedMessages_;
        queuedBytes_ += submission.bytes;
        ++connection->queuedMessages;
        connection->queuedBytes += submission.bytes;
        ++tickMessages_;
        tickBytes_ += submission.bytes;
        ++connection->tickMessages;
        connection->tickBytes += submission.bytes;
        connection->saturationTicks = 0;
        return Result<TransportBudgetDecision>::Success({TransportBudgetAdmission::Enqueued, ticket, 0});
    }

    /** @copydoc TransportBudgetController::Complete */
    Result<void> TransportBudgetController::Complete(const TransportQueueTicket ticket) {
        if (!ticket.IsValid() || ticket.Slot() >= queue_.size())
            return Fail<void>(NetworkErrors::TransportBudgetTicketStale);
        auto &entry = queue_[ticket.Slot()];
        if (!entry.occupied || entry.generation != ticket.Generation())
            return Fail<void>(NetworkErrors::TransportBudgetTicketStale);
        Release(entry);
        return Result<void>::Success();
    }

    /** @copydoc TransportBudgetController::Shutdown */
    std::size_t TransportBudgetController::Shutdown() noexcept {
        if (shuttingDown_)
            return 0;
        shuttingDown_ = true;
        const std::size_t discarded = queuedMessages_;
        for (auto &entry : queue_) {
            if (entry.occupied)
                Release(entry);
        }
        for (auto &connection : connections_)
            connection.active = false;
        activeConnections_ = 0;
        return discarded;
    }

    /** @copydoc TransportBudgetController::Snapshot */
    TransportBudgetSnapshot TransportBudgetController::Snapshot() const noexcept {
        return {policy_.revision, tick_, activeConnections_, queuedMessages_, queuedBytes_};
    }
}  // namespace Horo::Network
