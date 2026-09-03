#pragma once

/**
 * @file GameplayRegistration.h
 * @brief Typed project system, service, capability, scheduling, and lifecycle contracts.
 */

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Gameplay/Component.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Gameplay {
    inline constexpr std::size_t MaximumGameplayRegistrationIdBytes = 160;
    inline constexpr std::size_t MaximumGameplaySystems = 512;
    inline constexpr std::size_t MaximumGameplayServices = 256;
    inline constexpr std::size_t MaximumGameplayDependencies = 64;
    inline constexpr std::size_t MaximumGameplayCapabilities = 128;
    inline constexpr std::size_t MaximumGameplayComponentAccesses = 256;
    inline constexpr std::size_t MaximumGameplayObservabilityCategoryBytes = 160;

    /** @brief Stable identity of one project-owned scheduled system. */
    class GameplaySystemId final {
    public:
        GameplaySystemId() = default;

        /**
         * @brief Parses a project-owned system identity.
         * @param value Identifier using the `game.<module>.<system>` namespace.
         * @return Valid identity or a typed validation error.
         */
        [[nodiscard]] static Result<GameplaySystemId> Parse(std::string_view value);
        /** @brief Returns the stable identity text. */
        [[nodiscard]] const std::string &Value() const noexcept;
        /** @brief Reports whether this value contains a parsed identity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const GameplaySystemId &) const noexcept = default;

    private:
        explicit GameplaySystemId(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };

    /** @brief Stable identity of one project-owned service. */
    class GameplayServiceId final {
    public:
        GameplayServiceId() = default;

        /**
         * @brief Parses a project-owned service identity.
         * @param value Identifier using the `game.<module>.<service>` namespace.
         * @return Valid identity or a typed validation error.
         */
        [[nodiscard]] static Result<GameplayServiceId> Parse(std::string_view value);
        /** @brief Returns the stable identity text. */
        [[nodiscard]] const std::string &Value() const noexcept;
        /** @brief Reports whether this value contains a parsed identity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const GameplayServiceId &) const noexcept = default;

    private:
        explicit GameplayServiceId(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };

    /** @brief Stable identity of one capability required or provided by gameplay code. */
    class GameplayCapabilityId final {
    public:
        GameplayCapabilityId() = default;

        /**
         * @brief Parses a gameplay capability identity.
         * @param value Lowercase identifier with at least three namespace segments.
         * @return Valid identity or a typed validation error.
         */
        [[nodiscard]] static Result<GameplayCapabilityId> Parse(std::string_view value);
        /** @brief Returns the stable capability text. */
        [[nodiscard]] const std::string &Value() const noexcept;
        /** @brief Reports whether this value contains a parsed identity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const GameplayCapabilityId &) const noexcept = default;

    private:
        explicit GameplayCapabilityId(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };

    /** @brief Runtime phase in which one gameplay system executes. */
    enum class GameplaySystemPhase : std::uint8_t {
        PrePhysics,
        Physics,
        PostPhysics,
        Gameplay,
        Presentation,
        RenderExtraction,
    };

    /** @brief Execution domain permitted to invoke a service or system callback. */
    enum class GameplayThreadAffinity : std::uint8_t {
        RuntimeOwner,
        Worker,
        Any,
    };

    /** @brief Lifetime boundary for a project-owned gameplay service. */
    enum class GameplayServiceScope : std::uint8_t {
        Project,
        Scene,
    };

    /** @brief Behavior of one service when the active runtime scene is replaced. */
    enum class GameplaySceneReplacementPolicy : std::uint8_t {
        Preserve,
        Restart,
    };

    /** @brief Typed component read/write declaration used by schedule validation. */
    struct GameplayComponentAccessSet {
        std::vector<ComponentTypeId> reads;
        std::vector<ComponentTypeId> writes;
    };

    /** @brief Complete inert scheduling contract for one project-owned system. */
    struct GameplaySystemDescriptor {
        GameplaySystemId id;
        GameplaySystemPhase phase{GameplaySystemPhase::Gameplay};
        GameplayThreadAffinity affinity{GameplayThreadAffinity::RuntimeOwner};
        GameplayComponentAccessSet access;
        std::vector<GameplaySystemId> after;
        std::vector<GameplaySystemId> before;
        std::vector<GameplayServiceId> requiredServices;
        std::vector<GameplayCapabilityId> requiredCapabilities;
    };

    /** @brief Complete inert ownership and dependency contract for one game service. */
    struct GameplayServiceDescriptor {
        GameplayServiceId id;
        GameplayServiceScope scope{GameplayServiceScope::Project};
        GameplayThreadAffinity affinity{GameplayThreadAffinity::RuntimeOwner};
        GameplaySceneReplacementPolicy sceneReplacement{GameplaySceneReplacementPolicy::Preserve};
        std::vector<GameplayServiceId> dependencies;
        std::vector<GameplayCapabilityId> requiredCapabilities;
        std::vector<GameplayCapabilityId> providedCapabilities;
        std::string observabilityCategory;
    };

    /** @brief Narrow immutable capabilities passed to one service lifecycle callback. */
    struct GameplayServiceContext {
        CancellationToken cancellation;
        std::span<const GameplayServiceId> activeServices;
        std::span<const GameplayCapabilityId> capabilities;
        GameplayThreadAffinity thread{GameplayThreadAffinity::RuntimeOwner};
    };

    /** @brief Narrow immutable capabilities passed to one scheduled system callback. */
    struct GameplaySystemContext {
        CancellationToken cancellation;
        std::span<const GameplayServiceId> activeServices;
        std::span<const GameplayCapabilityId> capabilities;
        GameplaySystemPhase phase{GameplaySystemPhase::Gameplay};
        GameplayThreadAffinity thread{GameplayThreadAffinity::RuntimeOwner};
        double deltaSeconds{};
    };

    /** @brief Module-owned lifecycle implemented by one registered gameplay service. */
    class IGameplayService {
    public:
        virtual ~IGameplayService() = default;
        /** @brief Starts the service after all declared dependencies are active. */
        [[nodiscard]] virtual Result<void> Start(const GameplayServiceContext &context) = 0;
        /** @brief Stops the service after cancellation and before its providers are stopped. */
        virtual void Stop(const GameplayServiceContext &context) noexcept = 0;
    };

    /** @brief Module-owned lifecycle implemented by one registered gameplay system. */
    class IGameplaySystem {
    public:
        virtual ~IGameplaySystem() = default;
        /** @brief Starts the system after declared services and capabilities are available. */
        [[nodiscard]] virtual Result<void> Start(const GameplaySystemContext &context) = 0;
        /** @brief Executes one scheduled phase invocation. */
        [[nodiscard]] virtual Result<void> Execute(const GameplaySystemContext &context) = 0;
        /** @brief Stops the system after cancellation and before its dependencies are released. */
        virtual void Stop(const GameplaySystemContext &context) noexcept = 0;
    };

    /** @brief Exact-SDK-generation module-owned service construction and destruction callbacks. */
    struct GameplayServiceFactoryBinding {
        void *userData{};
        IGameplayService *(*create)(void *userData){};
        void (*destroy)(void *userData, IGameplayService *service) noexcept {};
    };

    /** @brief Exact-SDK-generation module-owned system construction and destruction callbacks. */
    struct GameplaySystemFactoryBinding {
        void *userData{};
        IGameplaySystem *(*create)(void *userData){};
        void (*destroy)(void *userData, IGameplaySystem *system) noexcept {};
    };

    /** @brief One service descriptor paired with its module-owned implementation factory. */
    struct GameplayServiceRegistration {
        GameplayServiceDescriptor descriptor;
        GameplayServiceFactoryBinding factory;
    };

    /** @brief One system descriptor paired with its module-owned implementation factory. */
    struct GameplaySystemRegistration {
        GameplaySystemDescriptor descriptor;
        GameplaySystemFactoryBinding factory;
    };
}  // namespace Horo::Gameplay
