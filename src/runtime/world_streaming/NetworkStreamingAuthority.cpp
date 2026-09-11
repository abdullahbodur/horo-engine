#include "Horo/WorldStreaming/NetworkStreamingAuthority.h"

#include "WorldStreamingInternal.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using Internal::Failure;

        [[nodiscard]] bool KnownIntent(const NetworkStreamingServerIntent intent) noexcept {
            return intent >= NetworkStreamingServerIntent::Release && intent <= NetworkStreamingServerIntent::RequireActive;
        }

        [[nodiscard]] bool KnownReadiness(const NetworkStreamingClientReadiness readiness) noexcept {
            return readiness >= NetworkStreamingClientReadiness::Pending && readiness <= NetworkStreamingClientReadiness::Failed;
        }

        template <typename Records> [[nodiscard]] auto FindCell(Records &records, const StreamingCellId &cell) {
            return std::ranges::lower_bound(records, cell, StreamingCellCanonicalLess{}, [](const NetworkStreamingAuthorityRecord &record) {
                return record.command.cell;
            });
        }

        [[nodiscard]] bool ReadyStateSatisfies(const NetworkStreamingServerIntent intent, const StreamingCellState state) noexcept {
            using enum NetworkStreamingServerIntent;
            using enum StreamingCellState;
            if (intent == RequireActive)
                return state == Active;
            return intent == RequireLoaded && (state == Resident || state == Active);
        }

        [[nodiscard]] Result<void> ValidateSequence(const std::optional<NetworkStreamingCommandSequence> current,
                                                    const NetworkStreamingCommandSequence candidate) {
            if (!current)
                return candidate.Value() == 1 ? Result<void>::Success()
                                              : Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
            const auto next = NextNetworkStreamingCommandSequence(*current);
            if (next.HasError())
                return Result<void>::Failure(next.ErrorValue());
            return candidate == next.Value() ? Result<void>::Success()
                                             : Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
        }

        [[nodiscard]] Result<void> ValidateServerCommand(const NetworkStreamingAuthorityConfig &config,
                                                         const NetworkStreamingAuthorityLifecycle lifecycle,
                                                         const std::optional<NetworkStreamingCommandSequence> lastSequence,
                                                         const NetworkStreamingIntentCommand &command) {
            if (!command.IsValid()) {
                return Failure<void>(KnownIntent(command.intent) ? WorldStreamingErrors::NetworkStreamingAuthorityInvalid
                                                                 : WorldStreamingErrors::NetworkStreamingAuthorityUnsupported);
            }
            if (lifecycle != NetworkStreamingAuthorityLifecycle::Active)
                return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityLifecycleUnavailable);
            if (command.session != config.session || command.partition != config.localOwner.partition)
                return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
            return ValidateSequence(lastSequence, command.sequence);
        }

        [[nodiscard]] Result<void> ValidateReadinessScope(const NetworkStreamingAuthorityConfig &config,
                                                          const NetworkStreamingAuthorityLifecycle lifecycle,
                                                          const StreamingRuntimeOwnerToken &localOwner,
                                                          const NetworkStreamingReadinessReport &report) {
            if (!localOwner.IsValid() || !report.session.IsValid() || !report.sequence.IsValid() || !report.partition.IsValid() ||
                !report.cell.IsValid())
                return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityInvalid);
            if (!KnownReadiness(report.readiness) || report.readiness == NetworkStreamingClientReadiness::Pending)
                return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityUnsupported);
            if (lifecycle == NetworkStreamingAuthorityLifecycle::Closed)
                return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityLifecycleUnavailable);
            if (localOwner != config.localOwner || report.session != config.session || report.partition != localOwner.partition)
                return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateReadyProof(const NetworkStreamingIntentCommand &command,
                                                      const StreamingRuntimeOwnerToken &localOwner,
                                                      const NetworkStreamingReadinessReport &report) {
            const bool ready = report.readiness == NetworkStreamingClientReadiness::Ready;
            if (!ready)
                return report.localFence || report.localState ? Failure<void>(WorldStreamingErrors::NetworkStreamingReadinessInvalid)
                                                              : Result<void>::Success();
            if (!report.localFence || !report.localState || !report.localFence->IsValid())
                return Failure<void>(WorldStreamingErrors::NetworkStreamingReadinessInvalid);
            if (report.localFence->partition != localOwner.partition || report.localFence->epoch != localOwner.epoch ||
                report.localFence->cell != report.cell)
                return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
            return ReadyStateSatisfies(command.intent, *report.localState)
                       ? Result<void>::Success()
                       : Failure<void>(WorldStreamingErrors::NetworkStreamingReadinessInvalid);
        }
    }  // namespace

    /** @copydoc NextNetworkStreamingCommandSequence */
    Result<NetworkStreamingCommandSequence> NextNetworkStreamingCommandSequence(const NetworkStreamingCommandSequence current) {
        if (!current.IsValid())
            return Failure<NetworkStreamingCommandSequence>(WorldStreamingErrors::NetworkStreamingAuthorityInvalid);
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<NetworkStreamingCommandSequence>(WorldStreamingErrors::GenerationExhausted);
        return NetworkStreamingCommandSequence::Create(current.Value() + 1);
    }

    /** @copydoc NetworkStreamingIntentCommand::IsValid */
    bool NetworkStreamingIntentCommand::IsValid() const noexcept {
        return session.IsValid() && sequence.IsValid() && partition.IsValid() && cell.IsValid() && KnownIntent(intent);
    }

    /** @copydoc NetworkStreamingAuthorityConfig::IsValid */
    bool NetworkStreamingAuthorityConfig::IsValid() const noexcept {
        return session.IsValid() && localOwner.IsValid() && maximumTrackedCells > 0 && maximumTrackedCells <= MaximumTrackedCells;
    }

    NetworkStreamingAuthority::NetworkStreamingAuthority(NetworkStreamingAuthorityConfig config,
                                                         std::vector<NetworkStreamingAuthorityRecord> records) noexcept
        : config_(config), records_(std::move(records)) {}

    /** @copydoc NetworkStreamingAuthority::NetworkStreamingAuthority */
    NetworkStreamingAuthority::NetworkStreamingAuthority(NetworkStreamingAuthority &&other) noexcept
        : config_(other.config_), records_(std::move(other.records_)), lastSequence_(other.lastSequence_), lifecycle_(other.lifecycle_) {
        other.records_.clear();
        other.lifecycle_ = NetworkStreamingAuthorityLifecycle::Closed;
    }

    /** @copydoc NetworkStreamingAuthority::Create */
    Result<NetworkStreamingAuthority> NetworkStreamingAuthority::Create(const NetworkStreamingAuthorityConfig &config) {
        if (!config.IsValid())
            return Failure<NetworkStreamingAuthority>(WorldStreamingErrors::NetworkStreamingAuthorityInvalid);
        try {
            std::vector<NetworkStreamingAuthorityRecord> records;
            records.reserve(config.maximumTrackedCells);
            return Result<NetworkStreamingAuthority>::Success(NetworkStreamingAuthority{config, std::move(records)});
        } catch (const std::bad_alloc &) {
            return Failure<NetworkStreamingAuthority>(WorldStreamingErrors::NetworkStreamingAuthorityCapacityExceeded);
        } catch (const std::length_error &) {
            return Failure<NetworkStreamingAuthority>(WorldStreamingErrors::NetworkStreamingAuthorityCapacityExceeded);
        }
    }

    /** @copydoc NetworkStreamingAuthority::ApplyServerIntent */
    Result<std::optional<NetworkStreamingAuthorityRecord>> NetworkStreamingAuthority::ApplyServerIntent(
        const NetworkStreamingIntentCommand &command) {
        if (const auto valid = ValidateServerCommand(config_, lifecycle_, lastSequence_, command); valid.HasError())
            return Result<std::optional<NetworkStreamingAuthorityRecord>>::Failure(valid.ErrorValue());

        auto found = FindCell(records_, command.cell);
        const bool exists = found != records_.end() && found->command.cell == command.cell;
        if (command.intent == NetworkStreamingServerIntent::Release) {
            if (exists)
                records_.erase(found);
            lastSequence_ = command.sequence;
            return Result<std::optional<NetworkStreamingAuthorityRecord>>::Success(std::nullopt);
        }
        if (!exists && records_.size() >= config_.maximumTrackedCells)
            return Failure<std::optional<NetworkStreamingAuthorityRecord>>(WorldStreamingErrors::NetworkStreamingAuthorityCapacityExceeded);

        const NetworkStreamingAuthorityRecord candidate{.command = command};
        if (exists)
            *found = candidate;
        else
            records_.insert(found, candidate);
        lastSequence_ = command.sequence;
        return Result<std::optional<NetworkStreamingAuthorityRecord>>::Success(candidate);
    }

    /** @copydoc NetworkStreamingAuthority::ApplyClientReadiness */
    Result<NetworkStreamingAuthorityRecord> NetworkStreamingAuthority::ApplyClientReadiness(const StreamingRuntimeOwnerToken &localOwner,
                                                                                            const NetworkStreamingReadinessReport &report) {
        if (const auto valid = ValidateReadinessScope(config_, lifecycle_, localOwner, report); valid.HasError())
            return Result<NetworkStreamingAuthorityRecord>::Failure(valid.ErrorValue());

        auto found = FindCell(records_, report.cell);
        if (found == records_.end() || found->command.cell != report.cell || found->command.sequence != report.sequence)
            return Failure<NetworkStreamingAuthorityRecord>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
        if (const auto valid = ValidateReadyProof(found->command, localOwner, report); valid.HasError())
            return Result<NetworkStreamingAuthorityRecord>::Failure(valid.ErrorValue());

        found->readiness = report.readiness;
        found->localFence = report.localFence;
        found->localState = report.localState;
        return Result<NetworkStreamingAuthorityRecord>::Success(*found);
    }

    /** @copydoc NetworkStreamingAuthority::RequestCancellation */
    Result<void> NetworkStreamingAuthority::RequestCancellation(const StreamingRuntimeOwnerToken &localOwner) noexcept {
        if (!localOwner.IsValid())
            return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityInvalid);
        if (localOwner != config_.localOwner)
            return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
        if (lifecycle_ == NetworkStreamingAuthorityLifecycle::Closed)
            return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityLifecycleUnavailable);
        lifecycle_ = NetworkStreamingAuthorityLifecycle::Cancelling;
        return Result<void>::Success();
    }

    /** @copydoc NetworkStreamingAuthority::Shutdown */
    Result<void> NetworkStreamingAuthority::Shutdown(const StreamingRuntimeOwnerToken &localOwner) noexcept {
        if (!localOwner.IsValid())
            return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityInvalid);
        if (localOwner != config_.localOwner)
            return Failure<void>(WorldStreamingErrors::NetworkStreamingAuthorityStale);
        records_.clear();
        lifecycle_ = NetworkStreamingAuthorityLifecycle::Closed;
        return Result<void>::Success();
    }

    /** @copydoc NetworkStreamingAuthority::Snapshot */
    std::span<const NetworkStreamingAuthorityRecord> NetworkStreamingAuthority::Snapshot() const noexcept {
        return records_;
    }

    /** @copydoc NetworkStreamingAuthority::LastSequence */
    std::optional<NetworkStreamingCommandSequence> NetworkStreamingAuthority::LastSequence() const noexcept {
        return lastSequence_;
    }

    /** @copydoc NetworkStreamingAuthority::Lifecycle */
    NetworkStreamingAuthorityLifecycle NetworkStreamingAuthority::Lifecycle() const noexcept {
        return lifecycle_;
    }
}  // namespace Horo::WorldStreaming
