#pragma once

/**
 * @file VfxIdentity.h
 * @brief Stable generation-safe VFX identities and canonical persistence encoding.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"
#include "Horo/Vfx/VfxErrors.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Horo::Vfx {
    namespace Detail {
        struct VfxIdentityScopeTag;
    }

    /** @brief Stable owner scope shared by editor, cooked artifacts, and one runtime realization. */
    using VfxIdentityScope = Foundation::Detail::NonZeroId64<Detail::VfxIdentityScopeTag, VfxErrors::IdentityInvalid>;

    /** @brief Canonical network-byte-order encoding of scope, slot, and generation. */
    using SerializedVfxIdentity = std::array<std::uint8_t, 16>;

    /** @brief Strong generation-safe identity in one tag-defined VFX resource domain. */
    template <typename Tag> struct VfxIdentity final {
        using IdentityTag = Tag;
        static constexpr std::uint32_t InvalidSlot = std::numeric_limits<std::uint32_t>::max();

        VfxIdentityScope scope{};        /**< Stable owning document/effect scope. */
        std::uint32_t slot{InvalidSlot}; /**< Owner-assigned logical slot. */
        std::uint32_t generation{};      /**< Non-zero non-wrapping slot generation. */

        /** @brief Checks representation, not current owner residency. @return True when all dimensions are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return scope.IsValid() && slot != InvalidSlot && generation != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const VfxIdentity &) const noexcept = default;
    };

    struct EffectSystemIdentityTag;
    struct EmitterIdentityTag;
    struct ParticleBufferIdentityTag;
    struct DecalIdentityTag;

    /** @brief Stable generation-safe identity of one logical effect-system instance. */
    using EffectSystemId = VfxIdentity<EffectSystemIdentityTag>;
    /** @brief Stable generation-safe identity of one emitter owned by an effect system. */
    using EmitterId = VfxIdentity<EmitterIdentityTag>;
    /** @brief Stable generation-safe identity of one logical particle buffer. */
    using ParticleBufferId = VfxIdentity<ParticleBufferIdentityTag>;
    /** @brief Stable generation-safe identity of one logical decal. */
    using DecalId = VfxIdentity<DecalIdentityTag>;

    /**
     * @brief Creates a validated VFX identity from owner-issued dimensions.
     * @param scope Stable owner scope.
     * @param slot Owner-assigned logical slot.
     * @param generation Non-zero current generation.
     * @return Typed identity or VfxErrors::IdentityInvalid.
     */
    template <typename Tag>
    [[nodiscard]] Result<VfxIdentity<Tag>> MakeVfxIdentity(const VfxIdentityScope scope, const std::uint32_t slot,
                                                           const std::uint32_t generation) {
        const VfxIdentity<Tag> identity{scope, slot, generation};
        if (!identity.IsValid())
            return Result<VfxIdentity<Tag>>::Failure(MakeError(VfxErrors::IdentityInvalid));
        return Result<VfxIdentity<Tag>>::Success(identity);
    }

    /**
     * @brief Encodes an identity in one fixed-width canonical network-byte-order representation.
     * @param identity Identity to persist through editor conversion or cook payloads.
     * @return Exact bytes; an invalid identity encodes its reserved values and will fail decoding.
     */
    template <typename Tag> [[nodiscard]] constexpr SerializedVfxIdentity SerializeVfxIdentity(const VfxIdentity<Tag> identity) noexcept {
        SerializedVfxIdentity bytes{};
        const auto write = [&bytes](const std::size_t offset, const std::uint64_t value, const std::size_t width) {
            for (std::size_t byte = 0; byte < width; ++byte) {
                const std::size_t shift = (width - byte - 1U) * 8U;
                bytes[offset + byte] = static_cast<std::uint8_t>(value >> shift);
            }
        };
        write(0, identity.scope.Value(), 8);
        write(8, identity.slot, 4);
        write(12, identity.generation, 4);
        return bytes;
    }

    /**
     * @brief Decodes and validates one canonical VFX identity.
     * @param bytes Fixed-width network-byte-order representation.
     * @return Exact typed identity or VfxErrors::SerializedIdentityInvalid.
     */
    template <typename Tag> [[nodiscard]] Result<VfxIdentity<Tag>> DeserializeVfxIdentity(const SerializedVfxIdentity &bytes) {
        const auto decode = [&bytes](const std::size_t offset, const std::size_t width) {
            std::uint64_t value{};
            for (std::size_t byte = 0; byte < width; ++byte)
                value = (value << 8U) | bytes[offset + byte];
            return value;
        };
        auto scope = VfxIdentityScope::Create(decode(0, 8));
        if (scope.HasError())
            return Result<VfxIdentity<Tag>>::Failure(MakeError(VfxErrors::SerializedIdentityInvalid));
        auto identity =
            MakeVfxIdentity<Tag>(scope.Value(), static_cast<std::uint32_t>(decode(8, 4)), static_cast<std::uint32_t>(decode(12, 4)));
        if (identity.HasError())
            return Result<VfxIdentity<Tag>>::Failure(MakeError(VfxErrors::SerializedIdentityInvalid));
        return identity;
    }

    /**
     * @brief Validates one submitted identity against the owner's exact current identity.
     * @param submitted Candidate supplied by a consumer.
     * @param current Current identity stored by the owner.
     * @return Success, IdentityInvalid, IdentityUnknown, or IdentityStale.
     */
    template <typename Tag>
    [[nodiscard]] Result<void> ValidateVfxIdentityAccess(const VfxIdentity<Tag> submitted, const VfxIdentity<Tag> current) {
        if (!submitted.IsValid() || !current.IsValid())
            return Result<void>::Failure(MakeError(VfxErrors::IdentityInvalid));
        if (submitted.scope != current.scope || submitted.slot != current.slot)
            return Result<void>::Failure(MakeError(VfxErrors::IdentityUnknown));
        if (submitted.generation != current.generation)
            return Result<void>::Failure(MakeError(VfxErrors::IdentityStale));
        return Result<void>::Success();
    }

    /**
     * @brief Advances one slot generation without wrap or slot/scope changes.
     * @param current Current valid identity.
     * @return Replacement identity, IdentityInvalid, or GenerationExhausted.
     */
    template <typename Tag> [[nodiscard]] Result<VfxIdentity<Tag>> AdvanceVfxIdentityGeneration(const VfxIdentity<Tag> current) {
        if (!current.IsValid())
            return Result<VfxIdentity<Tag>>::Failure(MakeError(VfxErrors::IdentityInvalid));
        if (current.generation == std::numeric_limits<std::uint32_t>::max())
            return Result<VfxIdentity<Tag>>::Failure(MakeError(VfxErrors::GenerationExhausted));
        return MakeVfxIdentity<Tag>(current.scope, current.slot, current.generation + 1U);
    }

    /** @brief Inert group of identities used to wire simulation and render consumers explicitly. */
    struct VfxIdentityComposition final {
        EffectSystemId effect{};      /**< Effect-system identity. */
        EmitterId emitter{};          /**< Emitter identity. */
        ParticleBufferId particles{}; /**< Particle-buffer identity. */
        DecalId decal{};              /**< Decal identity. */

        /** @brief Reports whether this is the explicit null composition. @return True when every identity is invalid. */
        [[nodiscard]] constexpr bool IsNull() const noexcept {
            return !effect.IsValid() && !emitter.IsValid() && !particles.IsValid() && !decal.IsValid();
        }

        /** @brief Reports whether every identity is usable. @return True for a complete composition. */
        [[nodiscard]] constexpr bool IsComplete() const noexcept {
            return effect.IsValid() && emitter.IsValid() && particles.IsValid() && decal.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const VfxIdentityComposition &) const noexcept = default;
    };

    /** @brief Returns an inert explicit null composition. @return A composition containing no usable identity. */
    [[nodiscard]] constexpr VfxIdentityComposition NullVfxIdentityComposition() noexcept {
        return {};
    }

    /**
     * @brief Creates a repeatable complete identity composition for downstream harnesses.
     * @param scope Stable non-zero harness scope.
     * @param firstSlot First of four consecutive slots.
     * @param generation Shared non-zero generation.
     * @return Complete composition or IdentityInvalid when dimensions cannot represent four identities.
     * @note This function is inert metadata construction; it performs no registration or ambient mutation.
     */
    [[nodiscard]] Result<VfxIdentityComposition> MakeDeterministicVfxIdentityComposition(VfxIdentityScope scope, std::uint32_t firstSlot,
                                                                                         std::uint32_t generation);
}  // namespace Horo::Vfx
