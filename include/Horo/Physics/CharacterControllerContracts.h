#pragma once

/** @file CharacterControllerContracts.h
 * @brief Backend-neutral Character identity, descriptor, movement and result contracts.
 */

#include "Horo/Foundation/Handles.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Physics/CharacterErrors.h"
#include "Horo/Physics/PhysicsQuery.h"
#include "Horo/Physics/PhysicsShapeDescriptor.h"

#include <array>
#include <optional>

namespace Horo::Character {
    /** @brief Absolute fixed capacity of one owned movement result. */
    inline constexpr std::uint32_t MaximumCharacterContacts = 32;
    /** @brief Maximum squared-norm error admitted for controller unit vectors and headings. */
    inline constexpr double CharacterUnitSquaredNormTolerance = 1.0e-6;

    /** @brief Process-local identity of one Character-world generation; never serialize it. */
    class CharacterWorldId final {
    public:
        /** @brief Constructs an invalid, unbound identity. */
        CharacterWorldId() = default;

        /**
         * @brief Validates a host-issued Character-world generation.
         * @param value Non-zero process-local generation.
         * @return Typed identity or CharacterErrors::WorldInvalid.
         */
        [[nodiscard]] static Result<CharacterWorldId> Create(std::uint64_t value);

        /** @brief Returns the host-issued value. @return Zero only for the invalid default identity. */
        [[nodiscard]] std::uint64_t Value() const noexcept;

        /** @brief Checks representation, not world liveness. @return Whether the value is non-zero. */
        [[nodiscard]] bool IsValid() const noexcept;

        constexpr auto operator<=>(const CharacterWorldId &) const noexcept = default;

    private:
        explicit constexpr CharacterWorldId(const std::uint64_t value) noexcept : value_(value) {}

        std::uint64_t value_{};
    };

    /** @brief Type discriminator for Character controller registry slots. */
    struct CharacterControllerTag;

    /**
     * @brief Non-owning process-local controller identity bound to one scene and Character world.
     *
     * Validation proves representation and owner-generation consistency only. The registry introduced
     * by CHR-001.3 must separately prove slot occupancy and the exact current slot generation.
     */
    struct CharacterControllerHandle final {
        std::uint64_t sceneGeneration{};           /**< Exact active scene generation. */
        CharacterWorldId world;                    /**< Exact paired Character-world generation. */
        Horo::Handle<CharacterControllerTag> slot; /**< Registry slot and non-zero slot generation. */

        /** @brief Checks identity shape, not registry liveness. @return True when every generation is usable. */
        [[nodiscard]] bool IsValid() const noexcept {
            return sceneGeneration != 0 && world.IsValid() && slot.IsValid() && slot.generation != 0;
        }

        constexpr auto operator<=>(const CharacterControllerHandle &) const noexcept = default;
    };

    /** @brief Explicit stance intent for one tick; no boolean or zero-value inference. */
    enum class CharacterStanceIntent : std::uint8_t {
        Keep,
        Stand,
        Crouch,
    };

    /** @brief Collision outcomes accumulated while resolving one movement request. */
    enum class CharacterCollisionFlags : std::uint16_t {
        None = 0,
        Sides = 1U << 0U,
        Ground = 1U << 1U,
        Ceiling = 1U << 2U,
        Step = 1U << 3U,
    };

    /** @brief Combines known collision evidence without converting it to an integerly typed API. */
    [[nodiscard]] constexpr CharacterCollisionFlags operator|(const CharacterCollisionFlags left,
                                                              const CharacterCollisionFlags right) noexcept {
        return static_cast<CharacterCollisionFlags>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }

    /**
     * @brief Owned inert controller creation policy for one exact scene/Character/Physics generation.
     *
     * The capsule uses normalized Horo meters and its +Y axis is aligned to `up`. Filtering reuses the
     * canonical project collision profile and query-channel identities. This descriptor allocates no
     * slot, resolves no registry and performs no native work.
     */
    struct CharacterControllerDescriptor final {
        std::uint64_t sceneGeneration{};
        CharacterWorldId characterWorld;
        Physics::PhysicsWorldId physicsWorld;
        Physics::PhysicsCapsuleShape capsule;
        Math::Vec3 collisionRootPosition;
        Math::Vec3 up{0, 1, 0};
        Math::Vec3 gravity{0, -9.81F, 0};
        Physics::CollisionProfileId collisionProfile;
        Physics::PhysicsQueryChannelId queryChannel;
        Physics::PhysicsQueryMaterial defaultMaterial;
        float skinWidthMeters{0.02F};
        float minimumMoveDistanceMeters{0.001F};
        float maximumStepHeightMeters{0.3F};
        float maximumSlopeDegrees{45.0F};
        std::uint32_t maximumContacts{16};
    };

    /**
     * @brief Owned fixed-tick movement intent without caller-selected delta time.
     *
     * Optional values distinguish absent translation/facing intent from a requested zero velocity or
     * identity heading. Character consumes the owning FixedStepContext quantum exactly once.
     */
    struct CharacterMovementRequest final {
        CharacterControllerHandle controller;
        std::uint64_t tick{};
        std::uint64_t sequence{};
        std::optional<Math::Vec3> desiredVelocityMetersPerSecond;
        std::optional<Math::Quaternion> desiredHeading;
        bool jumpRequested{};
        CharacterStanceIntent stance{CharacterStanceIntent::Keep};
    };

    /** @brief One owned solver-neutral surface contact retained in deterministic result order. */
    struct CharacterSurfaceContact final {
        std::optional<Physics::BodyHandle> body;
        Physics::ShapeHandle shape;
        Math::Vec3 point;
        Math::Vec3 normal{0, 1, 0};
        Physics::PhysicsQueryMaterial material;
        float penetrationDepthMeters{};
    };

    /**
     * @brief Owned bounded movement evidence for one committed controller tick.
     *
     * Only the first `contactCount` inline entries are active. `truncated` explicitly reports evidence
     * dropped at the descriptor's admitted bound; no result path performs a hidden heap allocation.
     */
    struct CharacterMovementResult final {
        CharacterControllerHandle controller;
        std::uint64_t tick{};
        std::uint64_t sequence{};
        Math::Vec3 finalPosition;
        Math::Quaternion finalHeading;
        Math::Vec3 achievedVelocityMetersPerSecond;
        Math::Vec3 up{0, 1, 0};
        bool grounded{};
        float groundSlopeDegrees{};
        Math::Vec3 groundNormal{0, 1, 0};
        std::optional<Physics::PhysicsQueryMaterial> groundMaterial;
        CharacterCollisionFlags collisions{CharacterCollisionFlags::None};
        std::array<CharacterSurfaceContact, MaximumCharacterContacts> contacts{};
        std::uint32_t contactCount{};
        bool truncated{};
    };

    /**
     * @brief Validates controller identity ownership without resolving a registry slot.
     * @param handle Borrowed controller identity.
     * @param expectedSceneGeneration Exact active scene generation.
     * @param expectedWorld Exact active Character-world generation.
     * @return Success or a stable malformed/world-mismatch error.
     */
    [[nodiscard]] Result<void> ValidateCharacterControllerHandleOwner(const CharacterControllerHandle &handle,
                                                                      std::uint64_t expectedSceneGeneration,
                                                                      CharacterWorldId expectedWorld);

    /**
     * @brief Validates inert geometry, basis, filtering and capacity policy.
     * @param descriptor Immutable controller creation policy.
     * @return Success or a stable world, descriptor, unsupported-operation or capacity error.
     * @post The descriptor is unchanged; success does not prove registry liveness or native support.
     */
    [[nodiscard]] Result<void> ValidateCharacterControllerDescriptor(const CharacterControllerDescriptor &descriptor);

    /**
     * @brief Validates one fixed-tick movement request and its owner generations.
     * @param request Immutable movement intent.
     * @param expectedSceneGeneration Exact active scene generation.
     * @param expectedWorld Exact active Character-world generation.
     * @return Success or a stable handle, request or unsupported-operation error.
     * @post The request is unchanged; success consumes no command and advances no sequence.
     */
    [[nodiscard]] Result<void> ValidateCharacterMovementRequest(const CharacterMovementRequest &request,
                                                                std::uint64_t expectedSceneGeneration, CharacterWorldId expectedWorld);

    /**
     * @brief Validates committed movement evidence against its admitted descriptor.
     * @param result Immutable owned result.
     * @param descriptor Descriptor whose inline-contact bound and owner generations apply.
     * @return Success or a stable handle, result, capacity or unsupported-operation error.
     * @post Both inputs remain unchanged. Success grants no mutation authority or lifetime extension.
     */
    [[nodiscard]] Result<void> ValidateCharacterMovementResult(const CharacterMovementResult &result,
                                                               const CharacterControllerDescriptor &descriptor);
}  // namespace Horo::Character
