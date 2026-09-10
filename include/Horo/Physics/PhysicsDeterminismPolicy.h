#pragma once

/** @file PhysicsDeterminismPolicy.h
 * @brief Versioned canonical command-order and random-stream seed contracts.
 */

#include "Horo/Foundation/Result.h"

#include <array>
#include <cstdint>

namespace Horo::Physics {
    /** @brief Version one of the canonical Physics command ordering protocol. */
    inline constexpr std::uint32_t PhysicsCommandOrderingProtocolV1 = 1;
    /** @brief Version one of the fixed Physics determinism fingerprint encoding. */
    inline constexpr std::uint32_t PhysicsDeterminismEncodingV1 = 1;
    /** @brief Version one of the immutable Physics seed-policy contract. */
    inline constexpr std::uint32_t PhysicsSeedPolicyContractV1 = 1;
    /** @brief Version one of the SplitMix64 Physics seed derivation algorithm. */
    inline constexpr std::uint32_t PhysicsSeedAlgorithmSplitMix64VersionV1 = 1;

    /** @brief Stable semantic target class; numerical order is the canonical cross-class tie-break. */
    enum class PhysicsCommandTargetKind : std::uint8_t {
        World = 0,
        Body = 1,
        Constraint = 2
    };

    /** @brief Structural intent category; numerical order is the canonical same-target tie-break. */
    enum class PhysicsStructuralCommandKind : std::uint8_t {
        Create = 0,
        Change = 1,
        Destroy = 2
    };

    /** @brief Non-zero stable identity of one command-producing authority, never a thread or process ID. */
    class PhysicsCommandSourceId final {
    public:
        /** @brief Constructs an invalid source identity. */
        PhysicsCommandSourceId() = default;
        /** @brief Validates a stable authored source identity. @param value Non-zero durable value.
         * @return Typed identity or PhysicsErrors::CommandOrderInvalid.
         */
        [[nodiscard]] static Result<PhysicsCommandSourceId> Create(std::uint64_t value);

        /** @brief Returns the stable value. @return Zero only for the invalid default identity. */
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation only. @return Whether the value is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        auto operator<=>(const PhysicsCommandSourceId &) const noexcept = default;

    private:
        explicit constexpr PhysicsCommandSourceId(const std::uint64_t value) : value_(value) {}

        std::uint64_t value_{};
    };

    /** @brief Complete canonical key for one simulation-affecting structural command. */
    struct PhysicsCommandOrderKey final {
        std::uint32_t protocolVersion{PhysicsCommandOrderingProtocolV1};      /**< Exact supported ordering protocol. */
        std::uint64_t simulationTick{};                                       /**< One-based authoritative tick receiving the command. */
        std::uint64_t worldGeneration{};                                      /**< Exact active PhysicsWorldId value. */
        std::uint64_t sceneGeneration{};                                      /**< Exact non-zero owning scene generation. */
        PhysicsCommandTargetKind targetKind{PhysicsCommandTargetKind::World}; /**< Stable semantic target class. */
        std::uint64_t targetIdentity{}; /**< Stable Horo identity, never a native solver ID or pointer. */
        PhysicsStructuralCommandKind commandKind{PhysicsStructuralCommandKind::Create}; /**< Structural operation. */
        PhysicsCommandSourceId source;  /**< Stable producer authority, independent of its executing worker. */
        std::uint64_t sourceSequence{}; /**< Non-zero sequence owned exclusively by source for this tick. */

        auto operator<=>(const PhysicsCommandOrderKey &) const noexcept = default;
    };

    /** @brief Validates a complete order key without consulting world state. @param key Candidate key.
     * @return Success or PhysicsErrors::CommandOrderInvalid.
     */
    [[nodiscard]] Result<void> ValidatePhysicsCommandOrderKey(const PhysicsCommandOrderKey &key);

    /** @brief Compares valid keys by the version-one canonical queue order. @param left First key. @param right Second key.
     * @return True when left precedes right in retained order; semantic safe points still select the apply phase.
     * Insertion and worker completion order are irrelevant.
     */
    [[nodiscard]] bool PhysicsCommandOrderLess(const PhysicsCommandOrderKey &left, const PhysicsCommandOrderKey &right) noexcept;

    /** @brief Closed deterministic pseudo-random seed derivation algorithms. */
    enum class PhysicsSeedAlgorithm : std::uint8_t {
        SplitMix64V1 = 0
    };

    /** @brief Non-zero named Physics random stream identity, never a display string or global RNG handle. */
    class PhysicsRandomStreamId final {
    public:
        /** @brief Constructs an invalid stream identity. */
        PhysicsRandomStreamId() = default;
        /** @brief Validates a stable named-stream value. @param value Non-zero reviewed stream identity.
         * @return Typed identity or PhysicsErrors::SeedPolicyInvalid.
         */
        [[nodiscard]] static Result<PhysicsRandomStreamId> Create(std::uint64_t value);

        /** @brief Returns the stable value. @return Zero only for the invalid default identity. */
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation only. @return Whether the value is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        auto operator<=>(const PhysicsRandomStreamId &) const noexcept = default;

    private:
        explicit constexpr PhysicsRandomStreamId(const std::uint64_t value) : value_(value) {}

        std::uint64_t value_{};
    };

    /** @brief Immutable session seed policy captured before Physics world activation. */
    struct PhysicsSeedPolicy final {
        std::uint32_t contractVersion{PhysicsSeedPolicyContractV1};              /**< Exact seed-policy schema. */
        std::uint64_t revision{};                                                /**< Non-zero immutable publication revision. */
        PhysicsSeedAlgorithm algorithm{PhysicsSeedAlgorithm::SplitMix64V1};      /**< Exact algorithm identity. */
        std::uint32_t algorithmVersion{PhysicsSeedAlgorithmSplitMix64VersionV1}; /**< Exact version; never silently upgraded. */
        std::uint64_t rootSeed{};        /**< Explicit product/session root seed; zero is a valid authored value. */
        std::uint64_t sessionSeed{};     /**< Explicit session domain; zero is a valid authored value. */
        std::uint64_t worldGeneration{}; /**< Non-zero world generation owning all derived streams. */
    };

    /** @brief Explicit consumption owner and position within one named stream. */
    struct PhysicsSeedConsumption final {
        PhysicsRandomStreamId stream;  /**< Named domain-separated stream. */
        std::uint64_t ownerIdentity{}; /**< Stable body/system owner, never a worker or native ID. */
        std::uint64_t sequence{};      /**< Non-zero consumption position owned only by ownerIdentity. */
    };

    /** @brief Fixed-size little-endian fingerprint input for build/session compatibility composition. */
    using PhysicsDeterminismPolicyEncoding = std::array<std::uint8_t, 52>;

    /** @brief Validates all seed policy fields and versions. @param policy Candidate immutable policy.
     * @return Success or PhysicsErrors::SeedPolicyInvalid.
     */
    [[nodiscard]] Result<void> ValidatePhysicsSeedPolicy(const PhysicsSeedPolicy &policy);

    /** @brief Derives one deterministic seed without ambient state or mutable consumption. @param policy Valid policy.
     * @param consumption Explicit stable stream owner and sequence.
     * @return Domain-separated 64-bit seed or PhysicsErrors::SeedPolicyInvalid.
     */
    [[nodiscard]] Result<std::uint64_t> DerivePhysicsSeed(const PhysicsSeedPolicy &policy, const PhysicsSeedConsumption &consumption);

    /** @brief Encodes order/seed policy identity in fixed little-endian field order. @param policy Valid policy.
     * @return Canonical bytes or PhysicsErrors::SeedPolicyInvalid.
     */
    [[nodiscard]] Result<PhysicsDeterminismPolicyEncoding> EncodePhysicsDeterminismPolicy(const PhysicsSeedPolicy &policy);
}  // namespace Horo::Physics
