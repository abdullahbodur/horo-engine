#include "Horo/Network/NetworkLifecycle.h"

#include "Horo/Network/NetworkErrors.h"

#include <new>
#include <utility>

namespace Horo::Network {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Fail(const ErrorCodeDescriptor &descriptor) {
            const Error failure = MakeError(descriptor);
            return Result<T>::Failure(failure);
        }

        [[nodiscard]] bool ValidLimits(const NetworkLifecycleLimits &limits) noexcept {
            return limits.maximumListeners > 0 && limits.maximumListeners <= MaximumNetworkLifecycleListeners &&
                   limits.maximumConnections > 0 && limits.maximumConnections <= MaximumNetworkLifecycleConnections;
        }

        [[nodiscard]] Result<NetworkLifecycleTerminal> MakeStandardTerminal(const NetworkLifecycleTerminalKind kind) {
            NetworkFailureKind failureKind{};
            switch (kind) {
                using enum NetworkLifecycleTerminalKind;
                case Cancelled:
                    failureKind = NetworkFailureKind::SessionCancelled;
                    break;
                case TimedOut:
                    failureKind = NetworkFailureKind::SessionTimedOut;
                    break;
                case Shutdown:
                    failureKind = NetworkFailureKind::SessionShutdown;
                    break;
                case Closed:
                case Failed:
                case Count:
                    return Fail<NetworkLifecycleTerminal>(NetworkErrors::NetworkLifecycleInvalid);
            }
            auto failure = MakeNetworkTerminalRecord(NetworkFailureLayer::Session, failureKind);
            if (failure.HasError())
                return Result<NetworkLifecycleTerminal>::Failure(failure.ErrorValue());
            return NetworkLifecycleTerminal::Create(kind, std::move(failure).Value());
        }

        [[nodiscard]] NetworkListenerState ListenerTerminalState(const NetworkLifecycleTerminalKind kind) noexcept {
            switch (kind) {
                using enum NetworkLifecycleTerminalKind;
                case Closed:
                    return NetworkListenerState::Closed;
                case Failed:
                case TimedOut:
                    return NetworkListenerState::Failed;
                case Cancelled:
                    return NetworkListenerState::Cancelled;
                case Shutdown:
                    return NetworkListenerState::ShuttingDown;
                case Count:
                    return NetworkListenerState::Failed;
            }
            return NetworkListenerState::Failed;
        }

        [[nodiscard]] NetworkConnectionState ConnectionTerminalState(const NetworkLifecycleTerminalKind kind) noexcept {
            switch (kind) {
                using enum NetworkLifecycleTerminalKind;
                case Closed:
                    return NetworkConnectionState::Closed;
                case Failed:
                    return NetworkConnectionState::Failed;
                case Cancelled:
                    return NetworkConnectionState::Cancelled;
                case TimedOut:
                    return NetworkConnectionState::TimedOut;
                case Shutdown:
                    return NetworkConnectionState::ShuttingDown;
                case Count:
                    return NetworkConnectionState::Failed;
            }
            return NetworkConnectionState::Failed;
        }

        template <typename Slots, typename Handle, typename Factory>
        [[nodiscard]] Result<void> AdmitPreparedSlot(Slots &slots, const Handle handle, Factory &&makeEntry) {
            const std::size_t index = handle.Slot();
            if (index >= slots.size())
                return Fail<void>(NetworkErrors::NetworkLifecycleCapacityExceeded);
            auto &slot = slots[index];
            if (!slot.has_value()) {
                slot.emplace(makeEntry());
                return Result<void>::Success();
            }
            if (slot->handle == handle)
                return Fail<void>(NetworkErrors::NetworkLifecycleInvalid);
            if (!slot->terminal.has_value())
                return Fail<void>(NetworkErrors::TerminalGenerationStale);
            if (auto expected = slot->handle.NextGeneration(); expected.HasError() || expected.Value() != handle)
                return Fail<void>(NetworkErrors::TerminalGenerationStale);
            slot.emplace(makeEntry());
            return Result<void>::Success();
        }

        template <typename Slots, typename Handle>
        [[nodiscard]] auto *FindExact(Slots &slots, const Handle handle, const NetworkOperationGeneration operation) noexcept {
            using Entry = typename Slots::value_type::value_type;
            const std::size_t index = handle.Slot();
            if (index >= slots.size())
                return static_cast<Entry *>(nullptr);
            auto &slot = slots[index];
            return slot.has_value() && slot->handle == handle && slot->operation == operation ? &*slot : nullptr;
        }

        template <typename Entry, typename State, typename Finalize>
        [[nodiscard]] Result<void> PublishFirstTerminal(Entry &entry, NetworkLifecycleTerminal terminal, const State closingState,
                                                        Finalize &&finalize) {
            if (entry.terminal.has_value())
                return Fail<void>(NetworkErrors::TerminalAlreadyResolved);
            if (terminal.Kind() == NetworkLifecycleTerminalKind::Closed && entry.state != closingState)
                return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
            finalize(entry, terminal.Kind());
            entry.terminal.emplace(std::move(terminal));
            return Result<void>::Success();
        }

        template <typename Complete> [[nodiscard]] Result<void> PublishCancellation(Complete &&complete) {
            auto terminal = MakeStandardTerminal(NetworkLifecycleTerminalKind::Cancelled);
            if (terminal.HasError())
                return Result<void>::Failure(terminal.ErrorValue());
            return complete(std::move(terminal).Value());
        }

        template <typename Slots, typename Finalize>
        std::size_t TerminalizeActive(Slots &slots, const NetworkLifecycleTerminal &terminal, Finalize &&finalize) {
            std::size_t completed{};
            for (auto &slot : slots) {
                if (!slot.has_value() || slot->terminal.has_value())
                    continue;
                finalize(*slot);
                slot->terminal.emplace(terminal);
                ++completed;
            }
            return completed;
        }

        template <typename Snapshot, typename Slots, typename Handle, typename Project>
        [[nodiscard]] Result<Snapshot> ProjectSnapshot(const Slots &slots, const Handle handle, Project &&project) {
            if (const std::size_t index = handle.Slot(); index < slots.size()) {
                const auto &slot = slots[index];
                if (slot.has_value() && slot->handle == handle)
                    return Result<Snapshot>::Success(project(*slot));
            }
            return Fail<Snapshot>(NetworkErrors::NetworkLifecycleOperationStale);
        }
    }  // namespace

    /** @copydoc NetworkOperationGeneration::Create */
    Result<NetworkOperationGeneration> NetworkOperationGeneration::Create(const std::uint64_t value) {
        if (value == 0)
            return Fail<NetworkOperationGeneration>(NetworkErrors::NetworkLifecycleInvalid);
        return Result<NetworkOperationGeneration>::Success(NetworkOperationGeneration{value});
    }

    NetworkLifecycleTerminal::NetworkLifecycleTerminal(const NetworkLifecycleTerminalKind kind,
                                                       std::optional<NetworkTerminalRecord> failure) noexcept
        : kind_(kind), failure_(std::move(failure)) {}

    /** @copydoc NetworkLifecycleTerminal::Create */
    Result<NetworkLifecycleTerminal> NetworkLifecycleTerminal::Create(const NetworkLifecycleTerminalKind kind,
                                                                      std::optional<NetworkTerminalRecord> failure) {
        using enum NetworkLifecycleTerminalKind;
        if (kind >= Count)
            return Fail<NetworkLifecycleTerminal>(NetworkErrors::NetworkLifecycleInvalid);
        if ((kind == Closed) == failure.has_value())
            return Fail<NetworkLifecycleTerminal>(NetworkErrors::NetworkLifecycleInvalid);
        if (failure.has_value()) {
            using enum NetworkFailureKind;
            const NetworkFailureKind observed = failure->Kind();
            if ((kind == Cancelled && observed != SessionCancelled) || (kind == TimedOut && observed != SessionTimedOut) ||
                (kind == Shutdown && observed != SessionShutdown) ||
                (kind == Failed && (observed == SessionCancelled || observed == SessionTimedOut || observed == SessionShutdown)))
                return Fail<NetworkLifecycleTerminal>(NetworkErrors::NetworkLifecycleInvalid);
        }
        return Result<NetworkLifecycleTerminal>::Success(NetworkLifecycleTerminal{kind, std::move(failure)});
    }

    /** @copydoc NetworkLifecycleTerminal::Failure */
    const NetworkTerminalRecord *NetworkLifecycleTerminal::Failure() const noexcept {
        return failure_.has_value() ? &*failure_ : nullptr;
    }

    NetworkLifecycleRegistry::NetworkLifecycleRegistry(std::vector<std::optional<ListenerEntry>> listeners,
                                                       std::vector<std::optional<ConnectionEntry>> connections) noexcept
        : listeners_(std::move(listeners)), connections_(std::move(connections)) {}

    /** @copydoc NetworkLifecycleRegistry::Create */
    Result<NetworkLifecycleRegistry> NetworkLifecycleRegistry::Create(const NetworkLifecycleLimits &limits) {
        if (!ValidLimits(limits))
            return Fail<NetworkLifecycleRegistry>(NetworkErrors::NetworkLifecycleInvalid);
        try {
            std::vector<std::optional<ListenerEntry>> listeners(limits.maximumListeners);
            std::vector<std::optional<ConnectionEntry>> connections(limits.maximumConnections);
            return Result<NetworkLifecycleRegistry>::Success(NetworkLifecycleRegistry{std::move(listeners), std::move(connections)});
        } catch (const std::bad_alloc &) {
            return Fail<NetworkLifecycleRegistry>(NetworkErrors::NetworkLifecycleCapacityExceeded);
        }
    }

    /** @copydoc NetworkLifecycleRegistry::AdmitListener */
    Result<void> NetworkLifecycleRegistry::AdmitListener(const ListenerHandle handle, const NetworkOperationGeneration operation) {
        if (shuttingDown_)
            return Fail<void>(NetworkErrors::TransportShuttingDown);
        if (!handle.IsValid() || !operation.IsValid())
            return Fail<void>(NetworkErrors::NetworkLifecycleInvalid);

        return AdmitPreparedSlot(listeners_, handle, [handle, operation] {
            return ListenerEntry{handle, operation, NetworkListenerState::Binding, {}};
        });
    }

    /** @copydoc NetworkLifecycleRegistry::MarkListening */
    Result<void> NetworkLifecycleRegistry::MarkListening(const ListenerHandle handle, const NetworkOperationGeneration operation) {
        ListenerEntry *entry = FindExact(listeners_, handle, operation);
        if (entry == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
        if (entry->terminal.has_value() || entry->state != NetworkListenerState::Binding)
            return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
        entry->state = NetworkListenerState::Listening;
        return Result<void>::Success();
    }

    /** @copydoc NetworkLifecycleRegistry::RequestListenerClose */
    Result<void> NetworkLifecycleRegistry::RequestListenerClose(const ListenerHandle handle, const NetworkOperationGeneration operation) {
        ListenerEntry *entry = FindExact(listeners_, handle, operation);
        if (entry == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
        if (!entry->terminal.has_value() && entry->state != NetworkListenerState::Closing)
            entry->state = NetworkListenerState::Closing;
        return Result<void>::Success();
    }

    /** @copydoc NetworkLifecycleRegistry::CompleteListener */
    Result<void> NetworkLifecycleRegistry::CompleteListener(const ListenerHandle handle, const NetworkOperationGeneration operation,
                                                            NetworkLifecycleTerminal terminal) {
        ListenerEntry *entry = FindExact(listeners_, handle, operation);
        if (entry == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
        return PublishFirstTerminal(*entry, std::move(terminal), NetworkListenerState::Closing,
                                    [](ListenerEntry &target, const NetworkLifecycleTerminalKind kind) {
            target.state = ListenerTerminalState(kind);
        });
    }

    /** @copydoc NetworkLifecycleRegistry::CancelListener */
    Result<void> NetworkLifecycleRegistry::CancelListener(const ListenerHandle handle, const NetworkOperationGeneration operation) {
        return PublishCancellation([this, handle, operation](NetworkLifecycleTerminal terminal) {
            return CompleteListener(handle, operation, std::move(terminal));
        });
    }

    /** @copydoc NetworkLifecycleRegistry::AdmitConnection */
    Result<void> NetworkLifecycleRegistry::AdmitConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation,
                                                           const bool requiresResolution, const std::uint64_t deadlineTick) {
        if (shuttingDown_)
            return Fail<void>(NetworkErrors::TransportShuttingDown);
        if (!handle.IsValid() || !operation.IsValid() || deadlineTick == 0)
            return Fail<void>(NetworkErrors::NetworkLifecycleInvalid);
        const NetworkConnectionState initial = requiresResolution ? NetworkConnectionState::Resolving : NetworkConnectionState::Created;

        return AdmitPreparedSlot(connections_, handle, [handle, operation, initial, deadlineTick] {
            return ConnectionEntry{handle, operation, initial, deadlineTick, {}};
        });
    }

    /** @copydoc NetworkLifecycleRegistry::AdvanceConnection */
    Result<void> NetworkLifecycleRegistry::AdvanceConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation,
                                                             const NetworkConnectionState next) {
        ConnectionEntry *entry = FindExact(connections_, handle, operation);
        if (entry == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
        if (entry->terminal.has_value())
            return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
        using enum NetworkConnectionState;
        if (const bool legal = ((entry->state == Created || entry->state == Resolving) && next == Connecting) ||
                               (entry->state == Connecting && next == AuthenticationReady);
            !legal)
            return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
        entry->state = next;
        if (next == AuthenticationReady)
            entry->deadlineTick = 0;
        return Result<void>::Success();
    }

    /** @copydoc NetworkLifecycleRegistry::RequestConnectionClose */
    Result<void> NetworkLifecycleRegistry::RequestConnectionClose(const ConnectionHandle handle,
                                                                  const NetworkOperationGeneration operation) {
        ConnectionEntry *entry = FindExact(connections_, handle, operation);
        if (entry == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
        if (!entry->terminal.has_value() && entry->state != NetworkConnectionState::Closing) {
            entry->state = NetworkConnectionState::Closing;
            entry->deadlineTick = 0;
        }
        return Result<void>::Success();
    }

    /** @copydoc NetworkLifecycleRegistry::CompleteConnection */
    Result<void> NetworkLifecycleRegistry::CompleteConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation,
                                                              NetworkLifecycleTerminal terminal) {
        ConnectionEntry *entry = FindExact(connections_, handle, operation);
        if (entry == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
        return PublishFirstTerminal(*entry, std::move(terminal), NetworkConnectionState::Closing,
                                    [](ConnectionEntry &target, const NetworkLifecycleTerminalKind kind) {
            target.state = ConnectionTerminalState(kind);
            target.deadlineTick = 0;
        });
    }

    /** @copydoc NetworkLifecycleRegistry::CancelConnection */
    Result<void> NetworkLifecycleRegistry::CancelConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation) {
        return PublishCancellation([this, handle, operation](NetworkLifecycleTerminal terminal) {
            return CompleteConnection(handle, operation, std::move(terminal));
        });
    }

    /** @copydoc NetworkLifecycleRegistry::ExpireConnections */
    Result<std::size_t> NetworkLifecycleRegistry::ExpireConnections(const std::uint64_t nowTick) {
        if (shuttingDown_)
            return Fail<std::size_t>(NetworkErrors::TransportShuttingDown);
        if (nowTick == 0)
            return Fail<std::size_t>(NetworkErrors::NetworkLifecycleInvalid);
        auto terminal = MakeStandardTerminal(NetworkLifecycleTerminalKind::TimedOut);
        if (terminal.HasError())
            return Result<std::size_t>::Failure(terminal.ErrorValue());

        std::size_t expired{};
        for (auto &slot : connections_) {
            if (!slot.has_value() || slot->terminal.has_value() || slot->deadlineTick == 0 || nowTick < slot->deadlineTick)
                continue;
            slot->state = NetworkConnectionState::TimedOut;
            slot->deadlineTick = 0;
            slot->terminal.emplace(terminal.Value());
            ++expired;
        }
        return Result<std::size_t>::Success(expired);
    }

    /** @copydoc NetworkLifecycleRegistry::Shutdown */
    Result<std::size_t> NetworkLifecycleRegistry::Shutdown() {
        if (shuttingDown_)
            return Result<std::size_t>::Success(0);
        auto terminal = MakeStandardTerminal(NetworkLifecycleTerminalKind::Shutdown);
        if (terminal.HasError())
            return Result<std::size_t>::Failure(terminal.ErrorValue());
        shuttingDown_ = true;

        std::size_t completed = TerminalizeActive(listeners_, terminal.Value(), [](ListenerEntry &entry) {
            entry.state = NetworkListenerState::ShuttingDown;
        });
        completed += TerminalizeActive(connections_, terminal.Value(), [](ConnectionEntry &entry) {
            entry.state = NetworkConnectionState::ShuttingDown;
            entry.deadlineTick = 0;
        });
        return Result<std::size_t>::Success(completed);
    }

    /** @copydoc NetworkLifecycleRegistry::Listener */
    Result<NetworkListenerSnapshot> NetworkLifecycleRegistry::Listener(const ListenerHandle handle) const {
        return ProjectSnapshot<NetworkListenerSnapshot>(listeners_, handle, [](const ListenerEntry &entry) {
            return NetworkListenerSnapshot{entry.handle, entry.operation, entry.state, entry.terminal};
        });
    }

    /** @copydoc NetworkLifecycleRegistry::Connection */
    Result<NetworkConnectionSnapshot> NetworkLifecycleRegistry::Connection(const ConnectionHandle handle) const {
        return ProjectSnapshot<NetworkConnectionSnapshot>(connections_, handle, [](const ConnectionEntry &entry) {
            return NetworkConnectionSnapshot{entry.handle, entry.operation, entry.state, entry.deadlineTick, entry.terminal};
        });
    }
}  // namespace Horo::Network
