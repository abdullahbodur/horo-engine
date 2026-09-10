#include "Horo/PlatformServices/PlatformRequest.h"

#include "Horo/PlatformServices/PlatformRequestErrors.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Horo::PlatformServices {
    struct PlatformRequestSubscription::Slot final {
        mutable std::mutex mutex;
        bool active{true};
        std::function<void(const PlatformRequestStore::ErasedSnapshot &)> observer;
        std::function<void()> release;

        void Reset() noexcept {
            std::function<void()> releaseOwner;
            {
                std::lock_guard lock(mutex);
                if (!active)
                    return;
                active = false;
                observer = {};
                releaseOwner = std::move(release);
            }
            if (releaseOwner)
                releaseOwner();
        }

        [[nodiscard]] bool IsActive() const noexcept {
            std::lock_guard lock(mutex);
            return active;
        }

        [[nodiscard]] std::function<void(const PlatformRequestStore::ErasedSnapshot &)> Take() noexcept {
            std::function<void(const PlatformRequestStore::ErasedSnapshot &)> callback;
            std::function<void()> releaseOwner;
            {
                std::lock_guard lock(mutex);
                if (!active)
                    return callback;
                active = false;
                callback = std::move(observer);
                releaseOwner = std::move(release);
            }
            if (releaseOwner)
                releaseOwner();
            return callback;
        }
    };

    struct PlatformRequestStore::State final {
        struct Record final {
            std::type_index type{typeid(void)};
            ErasedSnapshot snapshot;
            std::vector<std::weak_ptr<PlatformRequestSubscription::Slot>> observers;
        };

        struct Delivery final {
            std::shared_ptr<PlatformRequestSubscription::Slot> slot;
            ErasedSnapshot snapshot;
        };

        explicit State(PlatformRequestStoreConfig requested) : config(requested) {}

        PlatformRequestStoreConfig config;
        mutable std::mutex mutex;
        std::unordered_map<std::uint64_t, Record> records;
        std::deque<std::uint64_t> terminalOrder;
        std::deque<Delivery> deliveries;
        std::uint64_t nextId{1};
        std::size_t activeCount{};
        std::size_t observerCount{};
        std::uint64_t callbackFailures{};
        bool closed{};
        std::atomic_flag dispatching = ATOMIC_FLAG_INIT;
    };

    namespace {
        [[nodiscard]] bool CanComplete(const PlatformRequestState from, const PlatformRequestState to) noexcept {
            using enum PlatformRequestState;
            switch (from) {
                case Queued:
                    return to == Failed || to == TimedOut;
                case Running:
                    return to == Succeeded || to == Failed || to == TimedOut;
                case Cancelling:
                    return to == Succeeded || to == Failed || to == Cancelled || to == TimedOut;
                case Succeeded:
                case Failed:
                case Cancelled:
                case TimedOut:
                    return false;
            }
            return false;
        }

        [[nodiscard]] bool TerminalShapeIsValid(const PlatformRequestState state, const std::shared_ptr<const void> &value,
                                                const std::optional<Error> &error) noexcept {
            if (state == PlatformRequestState::Succeeded)
                return !error.has_value();
            if (!IsTerminal(state) || value || !error.has_value())
                return false;
            const auto &code = error->code.Value();
            const auto &domain = error->domain.Value();
            const auto isCancelled = code == RequestErrors::Cancelled.code.Value() && domain == RequestErrors::Cancelled.domain.Value();
            const auto isTimedOut = code == RequestErrors::TimedOut.code.Value() && domain == RequestErrors::TimedOut.domain.Value();
            if (state == PlatformRequestState::Cancelled)
                return isCancelled;
            if (state == PlatformRequestState::TimedOut)
                return isTimedOut;
            return !isCancelled && !isTimedOut;
        }

    }  // namespace

    /** @copydoc PlatformRequestSubscription::~PlatformRequestSubscription */
    PlatformRequestSubscription::~PlatformRequestSubscription() {
        Reset();
    }

    /** @copydoc PlatformRequestSubscription::PlatformRequestSubscription */
    PlatformRequestSubscription::PlatformRequestSubscription(PlatformRequestSubscription &&other) noexcept
        : slot_(std::move(other.slot_)) {}

    /** @copydoc PlatformRequestSubscription::operator= */
    PlatformRequestSubscription &PlatformRequestSubscription::operator=(PlatformRequestSubscription &&other) noexcept {
        if (this != &other) {
            Reset();
            slot_ = std::move(other.slot_);
        }
        return *this;
    }

    /** @copydoc PlatformRequestSubscription::Reset */
    void PlatformRequestSubscription::Reset() noexcept {
        if (slot_)
            slot_->Reset();
        slot_.reset();
    }

    /** @copydoc PlatformRequestSubscription::IsActive */
    bool PlatformRequestSubscription::IsActive() const noexcept {
        return slot_ && slot_->IsActive();
    }

    /** @copydoc PlatformRequestStore::PlatformRequestStore */
    PlatformRequestStore::PlatformRequestStore(PlatformRequestStoreConfig config) : state_(std::make_shared<State>(config)) {}

    /** @copydoc PlatformRequestStore::~PlatformRequestStore */
    PlatformRequestStore::~PlatformRequestStore() {
        Shutdown();
    }

    /** @copydoc PlatformRequestStore::Admit */
    Result<PlatformRequestId> PlatformRequestStore::AdmitErased(const std::type_index type) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(state_->mutex);
        if (state_->config.activeCapacity == 0 || state_->config.terminalCapacity == 0 || state_->config.observerCapacity == 0 ||
            !state_->config.generation.IsValid())
            return Result<PlatformRequestId>::Failure(MakeError(RequestErrors::InvalidConfiguration));
        if (state_->closed)
            return Result<PlatformRequestId>::Failure(MakeError(RequestErrors::FrontendUnavailable));
        if (state_->activeCount >= state_->config.activeCapacity)
            return Result<PlatformRequestId>::Failure(MakeError(RequestErrors::CapacityExceeded));
        if (state_->nextId == 0)
            return Result<PlatformRequestId>::Failure(MakeError(RequestErrors::CapacityExceeded, "Request identity space is exhausted."));

        const PlatformRequestId id{state_->nextId++};
        State::Record record{.type = type,
                             .snapshot = ErasedSnapshot{.id = id,
                                                        .generation = state_->config.generation,
                                                        .state = PlatformRequestState::Queued,
                                                        .timing = PlatformRequestTiming{.admittedAt = now}}};
        state_->records.emplace(id.value, std::move(record));
        ++state_->activeCount;
        return Result<PlatformRequestId>::Success(id);
    }

    /** @copydoc PlatformRequestStore::MarkRunning */
    Result<PlatformRequestMutation> PlatformRequestStore::MarkRunningErased(const PlatformRequestId id,
                                                                            const PlatformRequestGeneration generation,
                                                                            const std::type_index type) {
        std::lock_guard lock(state_->mutex);
        const auto found = state_->records.find(id.value);
        if (!id.IsValid() || generation != state_->config.generation || found == state_->records.end() || found->second.type != type)
            return Result<PlatformRequestMutation>::Failure(MakeError(RequestErrors::Stale));
        auto &snapshot = found->second.snapshot;
        if (snapshot.state == PlatformRequestState::Running)
            return Result<PlatformRequestMutation>::Success(PlatformRequestMutation::Unchanged);
        if (snapshot.state != PlatformRequestState::Queued)
            return Result<PlatformRequestMutation>::Failure(MakeError(RequestErrors::InvalidTransition));
        snapshot.state = PlatformRequestState::Running;
        snapshot.timing.startedAt = std::chrono::steady_clock::now();
        return Result<PlatformRequestMutation>::Success(PlatformRequestMutation::Applied);
    }

    /** @copydoc PlatformRequestStore::RequestCancel */
    Result<PlatformRequestMutation> PlatformRequestStore::RequestCancelErased(const PlatformRequestId id,
                                                                              const PlatformRequestGeneration generation,
                                                                              const std::type_index type) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(state_->mutex);
        const auto found = state_->records.find(id.value);
        if (!id.IsValid() || generation != state_->config.generation || found == state_->records.end() || found->second.type != type)
            return Result<PlatformRequestMutation>::Failure(MakeError(RequestErrors::Stale));
        auto &snapshot = found->second.snapshot;
        if (IsTerminal(snapshot.state) || snapshot.cancellationRequested)
            return Result<PlatformRequestMutation>::Success(PlatformRequestMutation::Unchanged);
        snapshot.cancellationRequested = true;
        snapshot.timing.cancellationRequestedAt = now;
        snapshot.state = PlatformRequestState::Cancelling;
        return Result<PlatformRequestMutation>::Success(PlatformRequestMutation::Applied);
    }

    /** @copydoc PlatformRequestStore::CompleteSuccess */
    Result<PlatformRequestMutation> PlatformRequestStore::CompleteSuccess(const PlatformRequestHandle<void> &handle) {
        return CompleteErased(handle.Id(), handle.Generation(), typeid(void), PlatformRequestState::Succeeded, {}, std::nullopt);
    }

    /** @copydoc PlatformRequestStore::CompleteFailure */
    Result<PlatformRequestMutation> PlatformRequestStore::CompleteErased(const PlatformRequestId id,
                                                                         const PlatformRequestGeneration generation,
                                                                         const std::type_index type,
                                                                         const PlatformRequestState terminalState,
                                                                         std::shared_ptr<const void> value, std::optional<Error> error) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(state_->mutex);
        const auto found = state_->records.find(id.value);
        if (!id.IsValid() || generation != state_->config.generation || found == state_->records.end() || found->second.type != type)
            return Result<PlatformRequestMutation>::Failure(MakeError(RequestErrors::Stale));
        auto &record = found->second;
        if (IsTerminal(record.snapshot.state))
            return Result<PlatformRequestMutation>::Success(PlatformRequestMutation::Unchanged);
        if (!CanComplete(record.snapshot.state, terminalState) || !TerminalShapeIsValid(terminalState, value, error))
            return Result<PlatformRequestMutation>::Failure(MakeError(RequestErrors::InvalidTransition));

        record.snapshot.state = terminalState;
        record.snapshot.terminal = true;
        record.snapshot.value = std::move(value);
        record.snapshot.error = std::move(error);
        record.snapshot.timing.terminalAt = now;
        --state_->activeCount;
        state_->terminalOrder.push_back(id.value);
        for (const auto &weak : record.observers)
            if (auto slot = weak.lock())
                state_->deliveries.push_back(State::Delivery{.slot = std::move(slot), .snapshot = record.snapshot});
        record.observers.clear();

        while (state_->terminalOrder.size() > state_->config.terminalCapacity) {
            const auto expired = state_->terminalOrder.front();
            state_->terminalOrder.pop_front();
            state_->records.erase(expired);
        }
        return Result<PlatformRequestMutation>::Success(PlatformRequestMutation::Applied);
    }

    /** @copydoc PlatformRequestStore::Query */
    Result<PlatformRequestStore::ErasedSnapshot> PlatformRequestStore::QueryErased(const PlatformRequestId id,
                                                                                   const PlatformRequestGeneration generation,
                                                                                   const std::type_index type) const {
        std::lock_guard lock(state_->mutex);
        if (!id.IsValid() || generation != state_->config.generation)
            return Result<ErasedSnapshot>::Failure(MakeError(RequestErrors::Stale));
        const auto found = state_->records.find(id.value);
        if (found == state_->records.end())
            return Result<ErasedSnapshot>::Failure(MakeError(RequestErrors::Expired));
        if (found->second.type != type)
            return Result<ErasedSnapshot>::Failure(MakeError(RequestErrors::Stale));
        return Result<ErasedSnapshot>::Success(found->second.snapshot);
    }

    /** @copydoc PlatformRequestStore::OnComplete */
    Result<PlatformRequestSubscription> PlatformRequestStore::SubscribeErased(const PlatformRequestId id,
                                                                              const PlatformRequestGeneration generation,
                                                                              const std::type_index type,
                                                                              std::function<void(const ErasedSnapshot &)> observer) {
        std::lock_guard lock(state_->mutex);
        if (state_->closed)
            return Result<PlatformRequestSubscription>::Failure(MakeError(RequestErrors::FrontendUnavailable));
        if (!id.IsValid() || generation != state_->config.generation)
            return Result<PlatformRequestSubscription>::Failure(MakeError(RequestErrors::Stale));
        const auto found = state_->records.find(id.value);
        if (found == state_->records.end())
            return Result<PlatformRequestSubscription>::Failure(MakeError(RequestErrors::Expired));
        if (found->second.type != type)
            return Result<PlatformRequestSubscription>::Failure(MakeError(RequestErrors::Stale));
        if (!observer || state_->observerCount >= state_->config.observerCapacity)
            return Result<PlatformRequestSubscription>::Failure(MakeError(RequestErrors::CapacityExceeded));

        auto slot = std::make_shared<PlatformRequestSubscription::Slot>();
        const std::weak_ptr<State> weakState = state_;
        const auto *slotIdentity = slot.get();
        slot->observer = std::move(observer);
        slot->release = [weakState, id, slotIdentity]() noexcept {
            if (const auto state = weakState.lock()) {
                std::lock_guard stateLock(state->mutex);
                if (state->observerCount > 0)
                    --state->observerCount;
                if (const auto record = state->records.find(id.value); record != state->records.end()) {
                    std::erase_if(record->second.observers, [slotIdentity](const auto &weak) {
                        const auto candidate = weak.lock();
                        return !candidate || candidate.get() == slotIdentity;
                    });
                }
                std::erase_if(state->deliveries, [slotIdentity](const auto &delivery) {
                    return delivery.slot.get() == slotIdentity;
                });
            }
        };
        ++state_->observerCount;
        if (found->second.snapshot.terminal)
            state_->deliveries.push_back(State::Delivery{.slot = slot, .snapshot = found->second.snapshot});
        else
            found->second.observers.emplace_back(slot);
        return Result<PlatformRequestSubscription>::Success(PlatformRequestSubscription{std::move(slot)});
    }

    /** @copydoc PlatformRequestStore::DispatchCompletions */
    std::size_t PlatformRequestStore::DispatchCompletions(const std::size_t maxCount) noexcept {
        if (maxCount == 0 || state_->dispatching.test_and_set(std::memory_order_acquire))
            return 0;

        struct DispatchGuard final {
            std::atomic_flag &flag;

            ~DispatchGuard() {
                flag.clear(std::memory_order_release);
            }
        } guard{state_->dispatching};

        std::size_t delivered{};
        while (delivered < maxCount) {
            State::Delivery delivery;
            {
                std::lock_guard lock(state_->mutex);
                if (state_->deliveries.empty())
                    break;
                delivery = std::move(state_->deliveries.front());
                state_->deliveries.pop_front();
            }
            auto observer = delivery.slot->Take();
            if (!observer)
                continue;
            try {
                observer(delivery.snapshot);
            } catch (...) {
                std::lock_guard lock(state_->mutex);
                ++state_->callbackFailures;
            }
            ++delivered;
        }
        return delivered;
    }

    /** @copydoc PlatformRequestStore::Shutdown */
    void PlatformRequestStore::Shutdown() noexcept {
        if (!state_)
            return;
        std::vector<std::shared_ptr<PlatformRequestSubscription::Slot>> observers;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->closed)
                return;
            state_->closed = true;
            const auto now = std::chrono::steady_clock::now();
            for (auto &[id, record] : state_->records) {
                if (!IsTerminal(record.snapshot.state)) {
                    record.snapshot.state = PlatformRequestState::Failed;
                    record.snapshot.terminal = true;
                    record.snapshot.error = MakeError(RequestErrors::FrontendUnavailable);
                    record.snapshot.timing.terminalAt = now;
                    state_->terminalOrder.push_back(id);
                }
                for (const auto &weak : record.observers)
                    if (auto slot = weak.lock())
                        observers.push_back(std::move(slot));
            }
            for (auto &delivery : state_->deliveries)
                observers.push_back(std::move(delivery.slot));
            state_->deliveries.clear();
            state_->activeCount = 0;
            while (state_->terminalOrder.size() > state_->config.terminalCapacity) {
                state_->records.erase(state_->terminalOrder.front());
                state_->terminalOrder.pop_front();
            }
        }
        for (const auto &observer : observers)
            observer->Reset();
    }

    /** @copydoc PlatformRequestStore::Generation */
    PlatformRequestGeneration PlatformRequestStore::Generation() const noexcept {
        return state_->config.generation;
    }

    /** @copydoc PlatformRequestStore::RecordCount */
    std::size_t PlatformRequestStore::RecordCount() const noexcept {
        std::lock_guard lock(state_->mutex);
        return state_->records.size();
    }

    /** @copydoc PlatformRequestStore::ObserverCount */
    std::size_t PlatformRequestStore::ObserverCount() const noexcept {
        std::lock_guard lock(state_->mutex);
        return state_->observerCount;
    }

    /** @copydoc PlatformRequestStore::CallbackFailureCount */
    std::uint64_t PlatformRequestStore::CallbackFailureCount() const noexcept {
        std::lock_guard lock(state_->mutex);
        return state_->callbackFailures;
    }
}  // namespace Horo::PlatformServices
