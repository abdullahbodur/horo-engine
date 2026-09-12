#include "Horo/Network/ReplicationRoles.h"

#include "Horo/Network/NetworkErrors.h"

namespace Horo::Network {
    namespace {
        template <typename T = void> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsKnown(const ReplicationExecutionRole role) noexcept {
            return role < ReplicationExecutionRole::Count;
        }

        [[nodiscard]] bool ValidFieldPolicy(const ReplicationFieldDescriptor &field) noexcept {
            return field.id.IsValid() && field.condition < ReplicationCondition::Count &&
                   field.writePolicy == ReplicationWritePolicy::AuthorityServerOnly &&
                   (field.condition == ReplicationCondition::Custom) == field.customCondition.has_value() &&
                   (!field.customCondition || field.customCondition->IsValid());
        }

        [[nodiscard]] bool SamePinnedIdentity(const ReplicationRoleBinding &left, const ReplicationRoleBinding &right) noexcept {
            return left.session == right.session && left.object == right.object && left.schema == right.schema &&
                   left.schemaVersion == right.schemaVersion && left.localPeer == right.localPeer;
        }
    }  // namespace

    /** @copydoc ReplicationRoleBinding::IsValid */
    bool ReplicationRoleBinding::IsValid() const noexcept {
        using enum ReplicationExecutionRole;
        if (!revision.IsValid() || !session.IsValid() || !object.IsValid() || !schema.IsValid() || !schemaVersion.IsValid() ||
            !IsKnown(role) || role == Standalone)
            return false;
        if ((localPeer && !localPeer->IsValid()) || (autonomousOwner && !autonomousOwner->IsValid()))
            return false;
        switch (role) {
            case AuthorityServer:
                return !localPeer.has_value();
            case AutonomousClient:
                return localPeer.has_value() && autonomousOwner.has_value() && localPeer == autonomousOwner;
            case SimulatedClient:
                return localPeer.has_value() && (!autonomousOwner || localPeer != autonomousOwner);
            case Standalone:
            case Count:
                return false;
        }
        return false;
    }

    /** @copydoc EvaluateReplicationCondition */
    Result<bool> EvaluateReplicationCondition(const ReplicationFieldDescriptor &field, const ReplicationRoleBinding &recipient,
                                              const ReplicationRecordKind record,
                                              const std::optional<ReplicationCustomConditionEvidence> customEvidence) {
        if (!recipient.IsValid() || !ValidFieldPolicy(field) || record >= ReplicationRecordKind::Count ||
            recipient.role == ReplicationExecutionRole::AuthorityServer ||
            (field.condition != ReplicationCondition::Custom && customEvidence.has_value()))
            return Failure<bool>(NetworkErrors::ReplicationRoleContextInvalid);

        switch (field.condition) {
            case ReplicationCondition::Always:
                return Result<bool>::Success(true);
            case ReplicationCondition::InitialOnly:
                return Result<bool>::Success(record == ReplicationRecordKind::Spawn);
            case ReplicationCondition::OwnerOnly:
                return Result<bool>::Success(recipient.role == ReplicationExecutionRole::AutonomousClient);
            case ReplicationCondition::SkipOwner:
            case ReplicationCondition::SimulatedOnly:
                return Result<bool>::Success(recipient.role == ReplicationExecutionRole::SimulatedClient);
            case ReplicationCondition::Custom:
                if (!customEvidence || !customEvidence->condition.IsValid() || customEvidence->condition != *field.customCondition)
                    return Failure<bool>(NetworkErrors::ReplicationRoleContextInvalid);
                return Result<bool>::Success(customEvidence->visible);
            case ReplicationCondition::Count:
                return Failure<bool>(NetworkErrors::ReplicationRoleContextInvalid);
        }
        return Failure<bool>(NetworkErrors::ReplicationRoleContextInvalid);
    }

    /** @copydoc AuthorizeReplicationFieldWrite */
    Result<void> AuthorizeReplicationFieldWrite(const ReplicationFieldDescriptor &field, const ReplicationRoleBinding &writer) {
        if (!writer.IsValid() || !ValidFieldPolicy(field))
            return Failure(NetworkErrors::ReplicationRoleContextInvalid);
        if (writer.role != ReplicationExecutionRole::AuthorityServer)
            return Failure(NetworkErrors::ReplicationAuthorityDenied);
        return Result<void>::Success();
    }

    /** @copydoc ReplicationRoleState::Create */
    Result<ReplicationRoleState> ReplicationRoleState::Create(const ReplicationRoleBinding &initial) {
        if (!initial.IsValid())
            return Failure<ReplicationRoleState>(NetworkErrors::ReplicationRoleContextInvalid);
        return Result<ReplicationRoleState>::Success(ReplicationRoleState{initial});
    }

    /** @copydoc ReplicationRoleState::ReplicationRoleState */
    ReplicationRoleState::ReplicationRoleState(const ReplicationRoleBinding &initial) noexcept
        : ownerThread_(std::this_thread::get_id()), current_(initial) {}

    /** @copydoc ReplicationRoleState::Stage */
    Result<void> ReplicationRoleState::Stage(const ReplicationRoleTransition &transition) {
        if (std::this_thread::get_id() != ownerThread_)
            return Failure(NetworkErrors::ReplicationRoleWrongThread);
        if (stopped_)
            return Failure(NetworkErrors::ReplicationRoleShuttingDown);
        if (pending_)
            return Failure(NetworkErrors::ReplicationRoleTransitionPending);
        if (transition.expectedRevision != current_.revision || transition.next.revision <= current_.revision)
            return Failure(NetworkErrors::ReplicationRoleTransitionStale);
        if (!transition.next.IsValid() || !SamePinnedIdentity(current_, transition.next))
            return Failure(NetworkErrors::ReplicationRoleContextInvalid);
        pending_ = transition.next;
        return Result<void>::Success();
    }

    /** @copydoc ReplicationRoleState::CommitAtSafePoint */
    Result<void> ReplicationRoleState::CommitAtSafePoint(const ReplicationRoleRevision revision) {
        if (std::this_thread::get_id() != ownerThread_)
            return Failure(NetworkErrors::ReplicationRoleWrongThread);
        if (stopped_)
            return Failure(NetworkErrors::ReplicationRoleShuttingDown);
        if (!pending_ || revision != pending_->revision)
            return Failure(NetworkErrors::ReplicationRoleTransitionStale);
        current_ = *pending_;
        pending_.reset();
        return Result<void>::Success();
    }

    /** @copydoc ReplicationRoleState::Snapshot */
    Result<ReplicationRoleBinding> ReplicationRoleState::Snapshot() const {
        if (std::this_thread::get_id() != ownerThread_)
            return Failure<ReplicationRoleBinding>(NetworkErrors::ReplicationRoleWrongThread);
        if (stopped_)
            return Failure<ReplicationRoleBinding>(NetworkErrors::ReplicationRoleShuttingDown);
        return Result<ReplicationRoleBinding>::Success(current_);
    }

    /** @copydoc ReplicationRoleState::Shutdown */
    Result<void> ReplicationRoleState::Shutdown() {
        if (std::this_thread::get_id() != ownerThread_)
            return Failure(NetworkErrors::ReplicationRoleWrongThread);
        stopped_ = true;
        pending_.reset();
        return Result<void>::Success();
    }
}  // namespace Horo::Network
