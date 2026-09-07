#include "Horo/Vfx/VfxIdentity.h"

namespace Horo::Vfx {
    namespace {
        enum class CompositionSlot : std::uint32_t {
            Effect,
            Emitter,
            ParticleBuffer,
            Decal,
            Count
        };

        template <typename Tag>
        VfxIdentity<Tag> ComposedIdentity(const VfxIdentityScope scope, const std::uint32_t firstSlot, const CompositionSlot offset,
                                          const std::uint32_t generation) noexcept {
            return {scope, firstSlot + static_cast<std::uint32_t>(offset), generation};
        }
    }  // namespace

    /** @copydoc MakeDeterministicVfxIdentityComposition */
    Result<VfxIdentityComposition> MakeDeterministicVfxIdentityComposition(const VfxIdentityScope scope, const std::uint32_t firstSlot,
                                                                           const std::uint32_t generation) {
        constexpr auto IdentityCount = static_cast<std::uint32_t>(CompositionSlot::Count);
        if (!scope.IsValid() || generation == 0 || firstSlot > EffectSystemId::InvalidSlot - IdentityCount)
            return Result<VfxIdentityComposition>::Failure(MakeError(VfxErrors::IdentityInvalid));

        return Result<VfxIdentityComposition>::Success({
            .effect = ComposedIdentity<EffectSystemIdentityTag>(scope, firstSlot, CompositionSlot::Effect, generation),
            .emitter = ComposedIdentity<EmitterIdentityTag>(scope, firstSlot, CompositionSlot::Emitter, generation),
            .particles = ComposedIdentity<ParticleBufferIdentityTag>(scope, firstSlot, CompositionSlot::ParticleBuffer, generation),
            .decal = ComposedIdentity<DecalIdentityTag>(scope, firstSlot, CompositionSlot::Decal, generation),
        });
    }
}  // namespace Horo::Vfx
