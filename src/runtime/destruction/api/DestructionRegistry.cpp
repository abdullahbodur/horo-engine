#include "Horo/Destruction/DestructionRegistry.h"

#include "Horo/Destruction/DestructionErrors.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Horo::Destruction {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        constexpr std::uint32_t KnownCommandCapabilities =
            (std::uint32_t{1} << static_cast<std::uint8_t>(DestructionCommandCapability::Count)) - 1U;

        /** @brief Checks product limits before constructing fixed storage. */
        [[nodiscard]] bool ValidRegistryLimits(const DestructionRegistryLimits &limits) noexcept {
            return limits.maximumEntries > 0 && limits.maximumEntries <= DestructionRegistryHardLimits::Entries &&
                   limits.maximumQueryResults > 0 && limits.maximumQueryResults <= DestructionRegistryHardLimits::QueryResults;
        }

        /** @brief Checks projected count ceilings against hard and internal bounds. */
        [[nodiscard]] bool ValidCountLimits(const DestructionLimits &limits) noexcept {
            if (limits.maximumChunksPerDestructible == 0 ||
                limits.maximumChunksPerDestructible > DestructionHardLimits::ChunksPerDestructible)
                return false;
            if (limits.maximumHierarchyDepth == 0 || limits.maximumHierarchyDepth > DestructionHardLimits::HierarchyDepth)
                return false;
            if (limits.maximumActiveChunkBodies == 0 || limits.maximumActiveChunkBodies > limits.maximumChunksPerDestructible)
                return false;
            if (limits.maximumEventsPerTransition == 0 || limits.maximumEventsPerTransition > DestructionHardLimits::EventsPerTransition)
                return false;
            if (limits.maximumEventJournalEntries < limits.maximumEventsPerTransition ||
                limits.maximumEventJournalEntries > DestructionHardLimits::EventJournalEntries)
                return false;
            return limits.maximumCosmeticDebrisParticles <= DestructionHardLimits::CosmeticDebrisParticles;
        }

        /** @brief Checks projected byte and work ceilings against hard and internal bounds. */
        [[nodiscard]] bool ValidCostLimits(const DestructionLimits &limits) noexcept {
            if (limits.maximumArtifactBytes == 0 || limits.maximumArtifactBytes > DestructionHardLimits::ArtifactBytes)
                return false;
            if (limits.maximumTransitionBytes == 0 || limits.maximumTransitionBytes > DestructionHardLimits::TransitionBytes)
                return false;
            if (limits.maximumResidentBytes < limits.maximumArtifactBytes || limits.maximumResidentBytes < limits.maximumTransitionBytes)
                return false;
            if (limits.maximumResidentBytes > DestructionHardLimits::ResidentBytes)
                return false;
            return limits.maximumWorkItemsPerTransition > 0 &&
                   limits.maximumWorkItemsPerTransition <= DestructionHardLimits::WorkItemsPerTransition;
        }

        /** @brief Checks one projected record without consulting ambient runtime state. */
        [[nodiscard]] bool ValidRecord(const DestructionRegistryRecord &record) noexcept {
            const auto &state = record.state;
            if (!state.target.IsValid() || !state.content.IsValid() || !state.configurationRevision.IsValid())
                return false;
            if (!state.effectiveFeatures.IsValid() || !state.revision.IsValid() || state.phase > DestructionStatePhase::Destroyed)
                return false;
            if (!std::isfinite(state.health) || state.health < 0.0F || !record.capabilityRevision.IsValid())
                return false;
            if ((record.commandCapabilities.bits & ~KnownCommandCapabilities) != 0U)
                return false;
            return ValidCountLimits(record.limits) && ValidCostLimits(record.limits);
        }

        /** @brief Tests whether two handles name the same authored owner independent of generation. */
        [[nodiscard]] bool SameOwner(const DestructionHandle left, const DestructionHandle right) noexcept {
            return left.world == right.world && left.destructible == right.destructible;
        }

        /** @brief Orders authored owners while deliberately excluding replacement generation. */
        [[nodiscard]] bool OwnerLess(const DestructionHandle left, const DestructionHandle right) noexcept {
            if (left.world != right.world)
                return left.world < right.world;
            return left.destructible < right.destructible;
        }

        /** @brief Maps a query phase filter to its exact state predicate. */
        [[nodiscard]] bool MatchesPhase(const DestructionPhaseFilter filter, const DestructionStatePhase phase) noexcept {
            switch (filter) {
                case DestructionPhaseFilter::Any:
                    return true;
                case DestructionPhaseFilter::Intact:
                    return phase == DestructionStatePhase::Intact;
                case DestructionPhaseFilter::Damaged:
                    return phase == DestructionStatePhase::Damaged;
                case DestructionPhaseFilter::Destroyed:
                    return phase == DestructionStatePhase::Destroyed;
                case DestructionPhaseFilter::Count:
                    return false;
            }
            return false;
        }

        /** @brief Tests exact feature inclusion without selecting a fallback profile. */
        [[nodiscard]] bool ContainsFeatures(const DestructionFeatureSet available, const DestructionFeatureSet required) noexcept {
            return (available.bits & required.bits) == required.bits;
        }

        /** @brief Finds one authored owner in a registry kept in owner order. */
        [[nodiscard]] auto FindOwner(const std::span<const DestructionRegistryRecord> records, const DestructionHandle target) noexcept {
            return std::ranges::lower_bound(records, target, OwnerLess, [](const DestructionRegistryRecord &record) {
                return record.state.target;
            });
        }

        /** @brief Resolves exact identity while distinguishing unknown owner from stale generation. */
        [[nodiscard]] Result<std::size_t> ResolveIndex(const std::span<const DestructionRegistryRecord> records,
                                                       const DestructionHandle target) {
            if (!target.IsValid())
                return Result<std::size_t>::Failure(MakeError(DestructionErrors::RegistryInvalid));
            const auto found = FindOwner(records, target);
            if (found == records.end() || !SameOwner(found->state.target, target))
                return Result<std::size_t>::Failure(MakeError(DestructionErrors::IdentityUnknown));
            if (found->state.target != target)
                return Result<std::size_t>::Failure(MakeError(DestructionErrors::StaleGeneration));
            return Result<std::size_t>::Success(static_cast<std::size_t>(found - records.begin()));
        }
    }  // namespace

    /** @copydoc DestructionQueryResult::Records */
    std::span<const DestructionRegistryRecord> DestructionQueryResult::Records() const noexcept {
        return {records_.data(), count_};
    }

    /** @copydoc DestructionRegistrySnapshot::Records */
    std::span<const DestructionRegistryRecord> DestructionRegistrySnapshot::Records() const noexcept {
        return {records_.data(), count_};
    }

    /** @copydoc DestructionRegistrySnapshot::Find */
    Result<DestructionRegistryRecord> DestructionRegistrySnapshot::Find(const DestructionHandle target) const {
        const auto index = ResolveIndex(Records(), target);
        if (index.HasError())
            return Result<DestructionRegistryRecord>::Failure(index.ErrorValue());
        return Result<DestructionRegistryRecord>::Success(records_[index.Value()]);
    }

    /** @copydoc DestructionRegistrySnapshot::Query */
    Result<DestructionQueryResult> DestructionRegistrySnapshot::Query(const DestructionQuery &query) const {
        if (!query.world.IsValid() || query.phase >= DestructionPhaseFilter::Count || !query.requiredFeatures.IsValid())
            return Result<DestructionQueryResult>::Failure(MakeError(DestructionErrors::RegistryInvalid));
        if (query.maximumResults == 0 || query.maximumResults > limits_.maximumQueryResults)
            return Result<DestructionQueryResult>::Failure(MakeError(DestructionErrors::RegistryCapacityExceeded));

        DestructionQueryResult result;
        result.revision_ = revision_;
        for (const DestructionRegistryRecord &record : Records()) {
            if (const auto &state = record.state; state.target.world != query.world || !MatchesPhase(query.phase, state.phase) ||
                                                  !ContainsFeatures(state.effectiveFeatures, query.requiredFeatures))
                continue;
            if (result.count_ == query.maximumResults) {
                result.hasMore_ = true;
                break;
            }
            result.records_[result.count_++] = record;
        }
        return Result<DestructionQueryResult>::Success(result);
    }

    /** @copydoc DestructionRegistrySnapshot::Capabilities */
    Result<DestructionCapabilitySnapshot> DestructionRegistrySnapshot::Capabilities(const DestructionHandle target) const {
        const auto record = Find(target);
        if (record.HasError())
            return Result<DestructionCapabilitySnapshot>::Failure(record.ErrorValue());
        const auto &value = record.Value();
        DestructionCapabilitySnapshot result;
        result.target_ = value.state.target;
        result.stateRevision_ = value.state.revision;
        result.registryRevision_ = revision_;
        result.capabilityRevision_ = value.capabilityRevision;
        result.features_ = value.state.effectiveFeatures;
        result.commands_ = value.commandCapabilities;
        result.limits_ = value.limits;
        return Result<DestructionCapabilitySnapshot>::Success(result);
    }

    DestructionRegistry::DestructionRegistry(const DestructionRegistryLimits limits, const DestructionRegistryRevision revision) noexcept
        : limits_(limits), revision_(revision) {}

    /** @copydoc DestructionRegistry::Create */
    Result<DestructionRegistry> DestructionRegistry::Create(const DestructionRegistryLimits &limits) {
        if (!ValidRegistryLimits(limits))
            return Result<DestructionRegistry>::Failure(MakeError(DestructionErrors::RegistryInvalid));
        auto initialRevision = DestructionRegistryRevision::Create(1);
        return Result<DestructionRegistry>::Success(DestructionRegistry{limits, initialRevision.Value()});
    }

    /** @copydoc DestructionRegistry::Register */
    Result<void> DestructionRegistry::Register(const DestructionRegistryRecord &record) {
        if (shutdown_)
            return Failure<void>(DestructionErrors::ShutdownInProgress);
        if (!ValidRecord(record))
            return Failure<void>(DestructionErrors::RegistryInvalid);
        const auto active = std::span<const DestructionRegistryRecord>{records_.data(), count_};
        const auto insertion = FindOwner(active, record.state.target);
        if (insertion != active.end() && SameOwner(insertion->state.target, record.state.target))
            return Failure<void>(DestructionErrors::RegistryDuplicate);
        if (count_ >= limits_.maximumEntries)
            return Failure<void>(DestructionErrors::RegistryCapacityExceeded);
        if (const auto advanced = AdvanceRevision(); advanced.HasError())
            return advanced;
        const auto insertionIndex = static_cast<std::size_t>(insertion - active.begin());
        std::move_backward(records_.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
                           records_.begin() + static_cast<std::ptrdiff_t>(count_),
                           records_.begin() + static_cast<std::ptrdiff_t>(count_ + 1));
        records_[insertionIndex] = record;
        ++count_;
        return Result<void>::Success();
    }

    /** @copydoc DestructionRegistry::Replace */
    Result<void> DestructionRegistry::Replace(const DestructionHandle current, const DestructionRegistryRecord &replacement) {
        if (shutdown_)
            return Failure<void>(DestructionErrors::ShutdownInProgress);
        if (!ValidRecord(replacement) || !SameOwner(current, replacement.state.target))
            return Failure<void>(DestructionErrors::RegistryInvalid);
        const auto active = std::span<const DestructionRegistryRecord>{records_.data(), count_};
        const auto index = ResolveIndex(active, current);
        if (index.HasError())
            return Result<void>::Failure(index.ErrorValue());
        const auto nextGeneration = AdvanceDestructionGeneration(current.generation);
        if (nextGeneration.HasError())
            return Result<void>::Failure(nextGeneration.ErrorValue());
        if (replacement.state.target.generation != nextGeneration.Value())
            return Failure<void>(DestructionErrors::StaleGeneration);
        if (const auto advanced = AdvanceRevision(); advanced.HasError())
            return advanced;
        records_[index.Value()] = replacement;
        return Result<void>::Success();
    }

    /** @copydoc DestructionRegistry::Remove */
    Result<bool> DestructionRegistry::Remove(const DestructionHandle target) {
        if (shutdown_)
            return Failure<bool>(DestructionErrors::ShutdownInProgress);
        const auto active = std::span<const DestructionRegistryRecord>{records_.data(), count_};
        const auto index = ResolveIndex(active, target);
        if (index.HasError())
            return Result<bool>::Failure(index.ErrorValue());
        if (const auto advanced = AdvanceRevision(); advanced.HasError())
            return Result<bool>::Failure(advanced.ErrorValue());
        std::move(records_.begin() + static_cast<std::ptrdiff_t>(index.Value()) + 1, records_.begin() + static_cast<std::ptrdiff_t>(count_),
                  records_.begin() + static_cast<std::ptrdiff_t>(index.Value()));
        records_[--count_] = {};
        return Result<bool>::Success(true);
    }

    /** @copydoc DestructionRegistry::Snapshot */
    Result<DestructionRegistrySnapshot> DestructionRegistry::Snapshot() const {
        if (shutdown_)
            return Failure<DestructionRegistrySnapshot>(DestructionErrors::ShutdownInProgress);
        DestructionRegistrySnapshot snapshot;
        snapshot.revision_ = revision_;
        snapshot.limits_ = limits_;
        snapshot.count_ = count_;
        std::ranges::copy_n(records_.begin(), count_, snapshot.records_.begin());
        return Result<DestructionRegistrySnapshot>::Success(snapshot);
    }

    /** @copydoc DestructionRegistry::BeginShutdown */
    void DestructionRegistry::BeginShutdown() noexcept {
        shutdown_ = true;
    }

    /** @copydoc DestructionRegistry::AdvanceRevision */
    Result<void> DestructionRegistry::AdvanceRevision() {
        if (revision_.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<void>(DestructionErrors::RevisionExhausted);
        revision_ = DestructionRegistryRevision::Create(revision_.Value() + 1U).Value();
        return Result<void>::Success();
    }
}  // namespace Horo::Destruction
