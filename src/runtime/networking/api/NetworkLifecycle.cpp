#include "Horo/Network/NetworkLifecycle.h"

#include "Horo/Network/NetworkErrors.h"

#include <new>
#include <utility>

namespace Horo::Network {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Fail(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
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
                    failureKind = NetworkFailureKind::SessionShuttingDown;
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
        if (kind >= NetworkLifecycleTerminalKind::Count)
            return Fail<NetworkLifecycleTerminal>(NetworkErrors::NetworkLifecycleInvalid);
        if ((kind == NetworkLifecycleTerminalKind::Closed) == failure.has_value())
            return Fail<NetworkLifecycleTerminal>(NetworkErrors::NetworkLifecycleInvalid);
        if (failure.has_value()) {
            const NetworkFailureKind observed = failure->Kind();
            if ((kind == NetworkLifecycleTerminalKind::Cancelled && observed != NetworkFailureKind::SessionCancelled) ||
                (kind == NetworkLifecycleTerminalKind::TimedOut && observed != NetworkFailureKind::SessionTimedOut) ||
                (kind == NetworkLifecycleTerminalKind::Shutdown && observed != NetworkFailureKind::SessionShuttingDown) ||
                (kind == NetworkLifecycleTerminalKind::Failed &&
                 (observed == NetworkFailureKind::SessionCancelled || observed == NetworkFailureKind::SessionTimedOut ||
                  observed == NetworkFailureKind::SessionShuttingDown)))
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

        std::optional<ListenerEntry> *freeSlot = nullptr;
        for (auto &slot : listeners_) {
            if (!slot.has_value()) {
                if (freeSlot == nullptr)
                    freeSlot = &slot;
                continue;
            }
            if (slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle == handle)
                return Fail<void>(NetworkErrors::NetworkLifecycleInvalid);
            if (!slot->terminal.has_value())
                return Fail<void>(NetworkErrors::TerminalGenerationStale);
            auto expected = slot->handle.NextGeneration();
            if (expected.HasError() || expected.Value() != handle)
                return Fail<void>(NetworkErrors::TerminalGenerationStale);
            slot.emplace(ListenerEntry{handle, operation, NetworkListenerState::Binding, {}});
            return Result<void>::Success();
        }
        if (freeSlot == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleCapacityExceeded);
        freeSlot->emplace(ListenerEntry{handle, operation, NetworkListenerState::Binding, {}});
        return Result<void>::Success();
    }

    /** @copydoc NetworkLifecycleRegistry::MarkListening */
    Result<void> NetworkLifecycleRegistry::MarkListening(const ListenerHandle handle, const NetworkOperationGeneration operation) {
        for (auto &slot : listeners_) {
            if (!slot.has_value() || slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle != handle || slot->operation != operation)
                return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
            if (slot->terminal.has_value() || slot->state != NetworkListenerState::Binding)
                return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
            slot->state = NetworkListenerState::Listening;
            return Result<void>::Success();
        }
        return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
    }

    /** @copydoc NetworkLifecycleRegistry::RequestListenerClose */
    Result<void> NetworkLifecycleRegistry::RequestListenerClose(const ListenerHandle handle, const NetworkOperationGeneration operation) {
        for (auto &slot : listeners_) {
            if (!slot.has_value() || slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle != handle || slot->operation != operation)
                return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
            if (slot->terminal.has_value() || slot->state == NetworkListenerState::Closing)
                return Result<void>::Success();
            slot->state = NetworkListenerState::Closing;
            return Result<void>::Success();
        }
        return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
    }

    /** @copydoc NetworkLifecycleRegistry::CompleteListener */
    Result<void> NetworkLifecycleRegistry::CompleteListener(const ListenerHandle handle, const NetworkOperationGeneration operation,
                                                            NetworkLifecycleTerminal terminal) {
        for (auto &slot : listeners_) {
            if (!slot.has_value() || slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle != handle || slot->operation != operation)
                return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
            if (slot->terminal.has_value())
                return Fail<void>(NetworkErrors::TerminalAlreadyResolved);
            if (terminal.Kind() == NetworkLifecycleTerminalKind::Closed && slot->state != NetworkListenerState::Closing)
                return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
            slot->state = ListenerTerminalState(terminal.Kind());
            slot->terminal.emplace(std::move(terminal));
            return Result<void>::Success();
        }
        return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
    }

    /** @copydoc NetworkLifecycleRegistry::CancelListener */
    Result<void> NetworkLifecycleRegistry::CancelListener(const ListenerHandle handle, const NetworkOperationGeneration operation) {
        auto terminal = MakeStandardTerminal(NetworkLifecycleTerminalKind::Cancelled);
        if (terminal.HasError())
            return Result<void>::Failure(terminal.ErrorValue());
        return CompleteListener(handle, operation, std::move(terminal).Value());
    }

    /** @copydoc NetworkLifecycleRegistry::AdmitConnection */
    Result<void> NetworkLifecycleRegistry::AdmitConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation,
                                                           const bool requiresResolution, const std::uint64_t deadlineTick) {
        if (shuttingDown_)
            return Fail<void>(NetworkErrors::TransportShuttingDown);
        if (!handle.IsValid() || !operation.IsValid() || deadlineTick == 0)
            return Fail<void>(NetworkErrors::NetworkLifecycleInvalid);
        const NetworkConnectionState initial = requiresResolution ? NetworkConnectionState::Resolving : NetworkConnectionState::Created;

        std::optional<ConnectionEntry> *freeSlot = nullptr;
        for (auto &slot : connections_) {
            if (!slot.has_value()) {
                if (freeSlot == nullptr)
                    freeSlot = &slot;
                continue;
            }
            if (slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle == handle)
                return Fail<void>(NetworkErrors::NetworkLifecycleInvalid);
            if (!slot->terminal.has_value())
                return Fail<void>(NetworkErrors::TerminalGenerationStale);
            auto expected = slot->handle.NextGeneration();
            if (expected.HasError() || expected.Value() != handle)
                return Fail<void>(NetworkErrors::TerminalGenerationStale);
            slot.emplace(ConnectionEntry{handle, operation, initial, deadlineTick, {}});
            return Result<void>::Success();
        }
        if (freeSlot == nullptr)
            return Fail<void>(NetworkErrors::NetworkLifecycleCapacityExceeded);
        freeSlot->emplace(ConnectionEntry{handle, operation, initial, deadlineTick, {}});
        return Result<void>::Success();
    }

    /** @copydoc NetworkLifecycleRegistry::AdvanceConnection */
    Result<void> NetworkLifecycleRegistry::AdvanceConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation,
                                                             const NetworkConnectionState next) {
        for (auto &slot : connections_) {
            if (!slot.has_value() || slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle != handle || slot->operation != operation)
                return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
            if (slot->terminal.has_value())
                return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
            const bool legal = ((slot->state == NetworkConnectionState::Created || slot->state == NetworkConnectionState::Resolving) &&
                                next == NetworkConnectionState::Connecting) ||
                               (slot->state == NetworkConnectionState::Connecting && next == NetworkConnectionState::AuthenticationReady);
            if (!legal)
                return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
            slot->state = next;
            if (next == NetworkConnectionState::AuthenticationReady)
                slot->deadlineTick = 0;
            return Result<void>::Success();
        }
        return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
    }

    /** @copydoc NetworkLifecycleRegistry::RequestConnectionClose */
    Result<void> NetworkLifecycleRegistry::RequestConnectionClose(const ConnectionHandle handle,
                                                                  const NetworkOperationGeneration operation) {
        for (auto &slot : connections_) {
            if (!slot.has_value() || slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle != handle || slot->operation != operation)
                return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
            if (slot->terminal.has_value() || slot->state == NetworkConnectionState::Closing)
                return Result<void>::Success();
            slot->state = NetworkConnectionState::Closing;
            slot->deadlineTick = 0;
            return Result<void>::Success();
        }
        return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
    }

    /** @copydoc NetworkLifecycleRegistry::CompleteConnection */
    Result<void> NetworkLifecycleRegistry::CompleteConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation,
                                                              NetworkLifecycleTerminal terminal) {
        for (auto &slot : connections_) {
            if (!slot.has_value() || slot->handle.Slot() != handle.Slot())
                continue;
            if (slot->handle != handle || slot->operation != operation)
                return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
            if (slot->terminal.has_value())
                return Fail<void>(NetworkErrors::TerminalAlreadyResolved);
            if (terminal.Kind() == NetworkLifecycleTerminalKind::Closed && slot->state != NetworkConnectionState::Closing)
                return Fail<void>(NetworkErrors::NetworkLifecycleTransitionInvalid);
            slot->state = ConnectionTerminalState(terminal.Kind());
            slot->deadlineTick = 0;
            slot->terminal.emplace(std::move(terminal));
            return Result<void>::Success();
        }
        return Fail<void>(NetworkErrors::NetworkLifecycleOperationStale);
    }

    /** @copydoc NetworkLifecycleRegistry::CancelConnection */
    Result<void> NetworkLifecycleRegistry::CancelConnection(const ConnectionHandle handle, const NetworkOperationGeneration operation) {
        auto terminal = MakeStandardTerminal(NetworkLifecycleTerminalKind::Cancelled);
        if (terminal.HasError())
            return Result<void>::Failure(terminal.ErrorValue());
        return CompleteConnection(handle, operation, std::move(terminal).Value());
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

        std::size_t completed{};
        for (auto &slot : listeners_) {
            if (!slot.has_value() || slot->terminal.has_value())
                continue;
            slot->state = NetworkListenerState::ShuttingDown;
            slot->terminal.emplace(terminal.Value());
            ++completed;
        }
        for (auto &slot : connections_) {
            if (!slot.has_value() || slot->terminal.has_value())
                continue;
            slot->state = NetworkConnectionState::ShuttingDown;
            slot->deadlineTick = 0;
            slot->terminal.emplace(terminal.Value());
            ++completed;
        }
        return Result<std::size_t>::Success(completed);
    }

    /** @copydoc NetworkLifecycleRegistry::Listener */
    Result<NetworkListenerSnapshot> NetworkLifecycleRegistry::Listener(const ListenerHandle handle) const {
        for (const auto &slot : listeners_) {
            if (slot.has_value() && slot->handle == handle)
                return Result<NetworkListenerSnapshot>::Success(
                    NetworkListenerSnapshot{slot->handle, slot->operation, slot->state, slot->terminal});
        }
        return Fail<NetworkListenerSnapshot>(NetworkErrors::NetworkLifecycleOperationStale);
    }

    /** @copydoc NetworkLifecycleRegistry::Connection */
    Result<NetworkConnectionSnapshot> NetworkLifecycleRegistry::Connection(const ConnectionHandle handle) const {
        for (const auto &slot : connections_) {
            if (slot.has_value() && slot->handle == handle)
                return Result<NetworkConnectionSnapshot>::Success(
                    NetworkConnectionSnapshot{slot->handle, slot->operation, slot->state, slot->deadlineTick, slot->terminal});
        }
        return Fail<NetworkConnectionSnapshot>(NetworkErrors::NetworkLifecycleOperationStale);
    }
}  // namespace Horo::Network
