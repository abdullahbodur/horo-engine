#include "Horo/Vfx/VfxIdentity.h"

namespace Horo::Vfx {
    /** @copydoc MakeDeterministicVfxIdentityComposition */
    Result<VfxIdentityComposition> MakeDeterministicVfxIdentityComposition(const VfxIdentityScope scope, const std::uint32_t firstSlot,
                                                                           const std::uint32_t generation) {
        if (!scope.IsValid() || generation == 0 || firstSlot > EffectSystemId::InvalidSlot - 4U)
            return Result<VfxIdentityComposition>::Failure(MakeError(VfxErrors::IdentityInvalid));

        return Result<VfxIdentityComposition>::Success({
            .effect = MakeVfxIdentity<EffectSystemIdentityTag>(scope, firstSlot, generation).Value(),
            .emitter = MakeVfxIdentity<EmitterIdentityTag>(scope, firstSlot + 1U, generation).Value(),
            .particles = MakeVfxIdentity<ParticleBufferIdentityTag>(scope, firstSlot + 2U, generation).Value(),
            .decal = MakeVfxIdentity<DecalIdentityTag>(scope, firstSlot + 3U, generation).Value(),
        });
    }
}  // namespace Horo::Vfx
