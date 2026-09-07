#include "Horo/Network/NetworkObjectIdentity.h"

namespace Horo::Network {
    /** @copydoc ReplicationAuthorityEpoch::Create */
    Result<ReplicationAuthorityEpoch> ReplicationAuthorityEpoch::Create(const std::uint64_t value) {
        if (value == 0)
            return Result<ReplicationAuthorityEpoch>::Failure(MakeError(NetworkErrors::NetworkObjectIdentityInvalid));
        return Result<ReplicationAuthorityEpoch>::Success(ReplicationAuthorityEpoch{value});
    }

    /** @copydoc NetworkObjectId::Create */
    Result<NetworkObjectId> NetworkObjectId::Create(const ReplicationAuthorityEpoch epoch, const std::uint64_t slot,
                                                    const std::uint32_t generation) {
        if (!epoch.IsValid() || slot == 0 || generation == 0)
            return Result<NetworkObjectId>::Failure(MakeError(NetworkErrors::NetworkObjectIdentityInvalid));
        return Result<NetworkObjectId>::Success(NetworkObjectId{epoch, slot, generation});
    }

    /** @copydoc NetworkObjectId::NextGeneration */
    Result<NetworkObjectId> NetworkObjectId::NextGeneration() const {
        if (!IsValid())
            return Result<NetworkObjectId>::Failure(MakeError(NetworkErrors::NetworkObjectIdentityInvalid));
        if (generation_ == std::numeric_limits<std::uint32_t>::max())
            return Result<NetworkObjectId>::Failure(MakeError(NetworkErrors::NetworkObjectGenerationExhausted));
        return Result<NetworkObjectId>::Success(NetworkObjectId{epoch_, slot_, generation_ + 1});
    }
}  // namespace Horo::Network
