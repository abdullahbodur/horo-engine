#pragma once

/** @file CharacterWorld.h
 * @brief Per-scene Character controller ownership and bounded slot lifecycle.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Physics/CharacterControllerContracts.h"
#include "Horo/Physics/CharacterWorldSettings.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Horo::Character {
    /** @brief Immutable owner generations selected for one detached Character-world candidate. */
    struct CharacterWorldDescriptor final {
        std::uint64_t sceneGeneration{};      /**< Exact scene generation that owns the world. */
        CharacterWorldId identity;            /**< Process-local Character-world generation. */
        Physics::PhysicsWorldId physicsWorld; /**< Exact paired Physics-world generation. */

        [[nodiscard]] constexpr auto operator<=>(const CharacterWorldDescriptor &) const noexcept = default;
    };

    /** @brief Observable lifecycle for a detached, published, or retired Character world. */
    enum class CharacterWorldState : std::uint8_t {
        Prepared,
        Active,
        Destroyed,
    };

    /**
     * @brief Owns bounded controller records for one exact scene and Physics-world generation.
     *
     * Preparation allocates the complete slot table before aggregate scene publication. Controller
     * descriptors may be installed while prepared so activation performs no allocation. Returned
     * handles are non-owning and remain valid only while their exact slot generation is resident.
     * The aggregate owner must shut this world down before its paired Physics world.
     */
    class CharacterWorld final {
    public:
        /**
         * @brief Creates an unpublished world candidate and reserves its complete controller capacity.
         * @param descriptor Exact scene, Character-world, and Physics-world owner generations.
         * @param settings Validated immutable settings snapshot copied into the candidate.
         * @return Prepared world, or a stable world/capacity error after complete rollback.
         * @post Success publishes no controller or scene state and retains no caller-owned storage.
         */
        [[nodiscard]] static Result<std::unique_ptr<CharacterWorld>> Prepare(const CharacterWorldDescriptor &descriptor,
                                                                             const CharacterWorldSettings &settings);

        /** @brief Drains controller records and retires the world if its aggregate owner omitted explicit shutdown. */
        ~CharacterWorld();
        CharacterWorld(const CharacterWorld &) = delete;
        CharacterWorld &operator=(const CharacterWorld &) = delete;

        /**
         * @brief Publishes a fully prepared candidate without allocating.
         * @return Success, or CharacterErrors::InvalidState unless the world is prepared.
         */
        [[nodiscard]] Result<void> Activate();

        /**
         * @brief Installs one validated owned controller descriptor into bounded world storage.
         * @param descriptor Inert descriptor bound to this world's exact owner generations.
         * @return Stable handle, or a typed descriptor/world/capacity/generation error.
         * @pre The aggregate owner serializes this call during candidate preparation or a declared
         * controller-lifecycle safe point. Concurrent fixed-tick admission is not supported.
         * @post Failure preserves every existing controller and slot generation.
         */
        [[nodiscard]] Result<CharacterControllerHandle> CreateController(const CharacterControllerDescriptor &descriptor);

        /**
         * @brief Removes one exact live controller generation and releases its owned record.
         * @param handle Handle issued by this world for a currently resident controller.
         * @return Success, or a typed malformed/foreign/stale/lifecycle error without mutation.
         * @pre The aggregate owner serializes this call during candidate preparation or a declared
         * controller-lifecycle safe point. Concurrent fixed-tick admission is not supported.
         */
        [[nodiscard]] Result<void> DestroyController(const CharacterControllerHandle &handle);

        /**
         * @brief Copies the inert descriptor for one exact live controller generation.
         * @param handle Handle issued by this world for a currently resident controller.
         * @return Owned descriptor copy, or a typed malformed/foreign/stale/lifecycle error.
         */
        [[nodiscard]] Result<CharacterControllerDescriptor> ControllerDescriptor(const CharacterControllerHandle &handle) const;

        /** @brief Closes admission and drains every controller record; safe repeatedly. */
        void Shutdown() noexcept;
        /** @brief Returns the current lifecycle state. @return Prepared, Active, or Destroyed. */
        [[nodiscard]] CharacterWorldState State() const noexcept;
        /** @brief Returns the immutable owner generations selected during preparation. @return Borrow valid for this world lifetime. */
        [[nodiscard]] const CharacterWorldDescriptor &Descriptor() const noexcept;
        /** @brief Returns the immutable settings snapshot retained for this world lifetime. @return Borrow valid for this world lifetime.
         */
        [[nodiscard]] const CharacterWorldSettings &Settings() const noexcept;
        /** @brief Returns the number of currently resident controllers. @return Live slot count; zero after shutdown. */
        [[nodiscard]] std::size_t ActiveControllerCount() const noexcept;
        /** @brief Returns the controller capacity reserved during preparation. @return Immutable slot count. */
        [[nodiscard]] std::size_t ControllerCapacity() const noexcept;

    private:
        struct Impl;
        /** @brief Takes a completely prepared implementation. */
        explicit CharacterWorld(std::unique_ptr<Impl> impl) noexcept;

        std::unique_ptr<Impl> impl_;
    };
}  // namespace Horo::Character
