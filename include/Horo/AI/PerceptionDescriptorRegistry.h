#pragma once

/**
 * @file PerceptionDescriptorRegistry.h
 * @brief Stable typed perception descriptor registry and immutable sensing-job snapshot.
 */

#include "Horo/AI/AIIdentity.h"
#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>

namespace Horo::AI {
    namespace SenseTypeIds {
        inline const SenseTypeId Sight = SenseTypeId::Create(1).Value();   /**< Built-in visual sense identity. */
        inline const SenseTypeId Hearing = SenseTypeId::Create(2).Value(); /**< Built-in acoustic sense identity. */
        inline const SenseTypeId Damage = SenseTypeId::Create(3).Value();  /**< Built-in damage-event sense identity. */
        inline const SenseTypeId Touch = SenseTypeId::Create(4).Value();   /**< Built-in contact sense identity. */
        inline const SenseTypeId Team = SenseTypeId::Create(5).Value();    /**< Built-in team-relay sense identity. */
    }  // namespace SenseTypeIds

    namespace StimulusTypeIds {
        inline const StimulusTypeId Visual = StimulusTypeId::Create(1).Value();  /**< Built-in visual observation payload. */
        inline const StimulusTypeId Sound = StimulusTypeId::Create(2).Value();   /**< Built-in acoustic event payload. */
        inline const StimulusTypeId Damage = StimulusTypeId::Create(3).Value();  /**< Built-in damage event payload. */
        inline const StimulusTypeId Contact = StimulusTypeId::Create(4).Value(); /**< Built-in contact payload. */
        inline const StimulusTypeId Team = StimulusTypeId::Create(5).Value();    /**< Built-in team awareness payload. */
    }  // namespace StimulusTypeIds

    /** @brief Origin family of an inert perception descriptor contribution. */
    enum class PerceptionDescriptorSourceKind : std::uint8_t {
        Native,
        Script,
        Package,
        Count,
    };

    /** @brief Stable origin metadata used for diagnostics, never for registration precedence. */
    struct PerceptionDescriptorOrigin final {
        PerceptionDescriptorSourceKind kind{PerceptionDescriptorSourceKind::Native}; /**< Contribution family. */
        PerceptionProviderId provider;                                               /**< Stable provider identity. */
        std::uint32_t version{1};                                                    /**< Non-zero descriptor contract version. */

        constexpr auto operator<=>(const PerceptionDescriptorOrigin &) const noexcept = default;
    };

    /** @brief Backend-neutral service capabilities required by perception descriptors. */
    enum class PerceptionCapability : std::uint8_t {
        SpatialCandidateQuery,
        PhysicsLineOfSight,
        AcousticOcclusion,
        PhysicsContact,
        TeamRelay,
        ScriptExecution,
        Count,
    };

    /** @brief Compact capability set safe to copy into immutable job snapshots. */
    struct PerceptionCapabilitySet final {
        std::uint32_t bits{}; /**< One bit per PerceptionCapability enumerator. */

        /** @brief Constructs a one-capability set. @param capability Capability bit to set. @return Typed capability set. */
        [[nodiscard]] static constexpr PerceptionCapabilitySet Of(const PerceptionCapability capability) noexcept {
            return {std::uint32_t{1} << static_cast<std::uint8_t>(capability)};
        }

        /** @brief Tests whether all requested capabilities are present. @param required Required subset. @return True on inclusion. */
        [[nodiscard]] constexpr bool Contains(const PerceptionCapabilitySet required) const noexcept {
            return (bits & required.bits) == required.bits;
        }

        /** @brief Combines capability requirements. @param other Additional requirements. @return Union set. */
        [[nodiscard]] constexpr PerceptionCapabilitySet Union(const PerceptionCapabilitySet other) const noexcept {
            return {bits | other.bits};
        }

        constexpr auto operator<=>(const PerceptionCapabilitySet &) const noexcept = default;
    };

    /** @brief Stable sense type metadata captured before SceneRuntime activation. */
    struct SenseTypeDescriptor final {
        SenseTypeId identity;                         /**< Save/wire identity; never derived from displayName. */
        PerceptionDescriptorOrigin origin;            /**< Owning native, script, or package contribution. */
        std::string displayName;                      /**< User-visible text only. */
        PerceptionCapabilitySet requiredCapabilities; /**< Services required to execute this sense. */

        bool operator==(const SenseTypeDescriptor &) const = default;
    };

    /** @brief Stable stimulus payload metadata captured before SceneRuntime activation. */
    struct StimulusTypeDescriptor final {
        StimulusTypeId identity;                      /**< Save/wire identity; never derived from displayName. */
        PerceptionDescriptorOrigin origin;            /**< Owning native, script, or package contribution. */
        std::string displayName;                      /**< User-visible text only. */
        std::uint32_t payloadVersion{1};              /**< Non-zero canonical payload schema version. */
        PerceptionCapabilitySet requiredCapabilities; /**< Services required to consume this payload. */

        bool operator==(const StimulusTypeDescriptor &) const = default;
    };

    /** @brief Policy for a listener whose referenced type or capability is unavailable. */
    enum class PerceptionDependencyPolicy : std::uint8_t {
        Required,
        Optional,
        Count,
    };

    /** @brief Hard bound for stimulus types accepted by one listener descriptor. */
    inline constexpr std::size_t MaximumStimulusTypesPerListener = 16;

    /** @brief Exact stimulus identity and compatible canonical payload-version interval. */
    struct PerceptionStimulusRequirement final {
        StimulusTypeId identity;                                                        /**< Required typed payload identity. */
        std::uint32_t minimumPayloadVersion{1};                                         /**< Inclusive minimum payload version. */
        std::uint32_t maximumPayloadVersion{std::numeric_limits<std::uint32_t>::max()}; /**< Inclusive maximum payload version. */

        constexpr auto operator<=>(const PerceptionStimulusRequirement &) const noexcept = default;
    };

    /** @brief Stable listener configuration resolved against sense and stimulus registries. */
    struct PerceptionListenerDescriptor final {
        PerceptionListenerTypeId identity;                                                    /**< Stable listener type identity. */
        PerceptionDescriptorOrigin origin;                                                    /**< Owning contribution. */
        std::string displayName;                                                              /**< User-visible text only. */
        SenseTypeId sense;                                                                    /**< Exact sense implementation dependency. */
        std::uint32_t minimumSenseVersion{1};                                                 /**< Inclusive accepted sense version. */
        std::uint32_t maximumSenseVersion{std::numeric_limits<std::uint32_t>::max()};         /**< Inclusive accepted sense version. */
        std::array<PerceptionStimulusRequirement, MaximumStimulusTypesPerListener> stimuli{}; /**< Accepted typed payloads and versions. */
        std::size_t stimulusCount{};                                                          /**< Active prefix of stimuli. */
        PerceptionCapabilitySet requiredCapabilities;                                      /**< Listener-specific service requirements. */
        PerceptionDependencyPolicy dependencyPolicy{PerceptionDependencyPolicy::Required}; /**< Missing dependency behavior. */

        bool operator==(const PerceptionListenerDescriptor &) const = default;
    };

    /** @brief Deterministic listener admission state in an immutable registry snapshot. */
    enum class PerceptionListenerAvailability : std::uint8_t {
        Available,
        MissingSense,
        MissingStimulus,
        IncompatibleSense,
        IncompatibleStimulus,
        CapabilityUnavailable,
    };

    /** @brief Captured listener plus typed availability outcome. */
    struct PerceptionListenerResolution final {
        PerceptionListenerDescriptor descriptor;       /**< Owned canonical descriptor. */
        PerceptionListenerAvailability availability{}; /**< Exact activation result. */

        bool operator==(const PerceptionListenerResolution &) const = default;
    };

    /** @brief Compile-time storage ceilings protecting registry construction from hostile input. */
    struct PerceptionDescriptorRegistryHardLimits final {
        static constexpr std::size_t SenseTypes = 64;        /**< Maximum captured sense descriptors. */
        static constexpr std::size_t StimulusTypes = 128;    /**< Maximum captured stimulus descriptors. */
        static constexpr std::size_t ListenerTypes = 128;    /**< Maximum captured listener descriptors. */
        static constexpr std::size_t DisplayNameBytes = 256; /**< Maximum bytes in one diagnostic display name. */
    };

    /** @brief Product-selected admission limits bounded by PerceptionDescriptorRegistryHardLimits. */
    struct PerceptionDescriptorRegistryLimits final {
        std::size_t maximumSenseTypes{PerceptionDescriptorRegistryHardLimits::SenseTypes};
        std::size_t maximumStimulusTypes{PerceptionDescriptorRegistryHardLimits::StimulusTypes};
        std::size_t maximumListenerTypes{PerceptionDescriptorRegistryHardLimits::ListenerTypes};
        std::size_t maximumDisplayNameBytes{128};
    };

    /** @brief Borrowed contributions copied synchronously into one immutable registry snapshot. */
    struct PerceptionDescriptorContributions final {
        std::span<const SenseTypeDescriptor> senses;             /**< Borrowed sense contributions. */
        std::span<const StimulusTypeDescriptor> stimuli;         /**< Borrowed stimulus contributions. */
        std::span<const PerceptionListenerDescriptor> listeners; /**< Borrowed listener contributions. */
    };

    /**
     * @brief Immutable, identity-sorted perception registry safe to borrow throughout sensing jobs.
     * @details Capture is inert metadata composition. It does not activate services, select backends, or mutate ambient state.
     */
    class PerceptionDescriptorRegistry final {
    public:
        PerceptionDescriptorRegistry() = delete;
        /** @brief Releases owned snapshot storage after all borrowing sensing jobs have completed. */
        ~PerceptionDescriptorRegistry();

        /**
         * @brief Validates, resolves, and owns one bounded descriptor snapshot.
         * @param contributions Borrowed native, script, and package contributions.
         * @param availableCapabilities Explicit capabilities supplied by the composition root.
         * @param limits Product-selected hard-bounded capacities.
         * @return Immutable registry or typed invalid, conflict, missing, unavailable, capacity, or storage failure.
         * @throws std::bad_alloc When copying bounded display-name storage fails.
         * @post Optional dependency failures affect only their listener resolution.
         */
        [[nodiscard]] static Result<PerceptionDescriptorRegistry> Capture(const PerceptionDescriptorContributions &contributions,
                                                                          PerceptionCapabilitySet availableCapabilities,
                                                                          const PerceptionDescriptorRegistryLimits &limits = {});

        /** @brief Returns sense descriptors in stable identity order. @return Registry-owned immutable view. */
        [[nodiscard]] std::span<const SenseTypeDescriptor> Senses() const noexcept;
        /** @brief Returns stimulus descriptors in stable identity order. @return Registry-owned immutable view. */
        [[nodiscard]] std::span<const StimulusTypeDescriptor> Stimuli() const noexcept;
        /** @brief Returns listener resolutions in stable identity order. @return Registry-owned immutable view. */
        [[nodiscard]] std::span<const PerceptionListenerResolution> Listeners() const noexcept;

        /** @brief Resolves a sense by stable identity. @param identity Exact identity. @return Descriptor or typed unknown failure. */
        [[nodiscard]] Result<SenseTypeDescriptor> FindSense(SenseTypeId identity) const;
        /** @brief Resolves a stimulus by stable identity. @param identity Exact identity. @return Descriptor or typed unknown failure. */
        [[nodiscard]] Result<StimulusTypeDescriptor> FindStimulus(StimulusTypeId identity) const;
        /** @brief Resolves a listener by stable identity. @param identity Exact identity. @return Resolution or typed unknown failure. */
        [[nodiscard]] Result<PerceptionListenerResolution> FindListener(PerceptionListenerTypeId identity) const;

        PerceptionDescriptorRegistry(const PerceptionDescriptorRegistry &) = delete;
        /** @brief Transfers snapshot ownership without copying descriptor storage. */
        PerceptionDescriptorRegistry(PerceptionDescriptorRegistry &&) noexcept;
        PerceptionDescriptorRegistry &operator=(const PerceptionDescriptorRegistry &) = delete;
        PerceptionDescriptorRegistry &operator=(PerceptionDescriptorRegistry &&) = delete;

    private:
        struct Storage;
        explicit PerceptionDescriptorRegistry(std::unique_ptr<Storage> storage) noexcept;
        std::unique_ptr<Storage> storage_;
    };
}  // namespace Horo::AI
