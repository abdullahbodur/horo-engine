#pragma once

/**
 * @file ModuleDescriptor.h
 * @brief Inert built-in module metadata and pre-composition graph validation.
 */

#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Horo {
    /** @brief Stable identity of an internal module. */
    struct ModuleId {
        std::string value; /**< Canonical namespaced identity, such as `horo.foundation`. */

        [[nodiscard]] auto operator<=>(const ModuleId &) const = default;
    };

    /** @brief Stable identity of a capability supplied or required by a module. */
    struct ModuleCapabilityId {
        std::string value; /**< Canonical namespaced capability identity. */

        [[nodiscard]] auto operator<=>(const ModuleCapabilityId &) const = default;
    };

    /** @brief Semantic version of one module's public contract. */
    struct ModuleContractVersion {
        std::uint32_t major{}; /**< Breaking contract generation. */
        std::uint32_t minor{}; /**< Backward-compatible capability generation. */
        std::uint32_t patch{}; /**< Backward-compatible correction generation. */

        [[nodiscard]] auto operator<=>(const ModuleContractVersion &) const = default;
    };

    /** @brief Whether an absent module dependency invalidates composition. */
    enum class ModuleDependencyKind : std::uint8_t {
        Required,
        Optional,
    };

    /** @brief Versioned dependency on another internal module. */
    struct ModuleDependency {
        ModuleId module;                                           /**< Provider module identity. */
        ModuleContractVersion minimumVersion{};                    /**< Minimum compatible provider contract. */
        ModuleDependencyKind kind{ModuleDependencyKind::Required}; /**< Absence policy. */
    };

    /** @brief Resource category described by a module budget hint. */
    enum class ModuleResourceBudgetKind : std::uint8_t {
        MemoryBytes,
        QueueDepth,
        ConcurrentJobs,
        DedicatedThreads,
        CaptureBytesPerSecond,
    };

    /** @brief Execution domain associated with a resource budget hint. */
    enum class ModuleThreadAffinity : std::uint8_t {
        Any,
        Main,
        Render,
        Worker,
        Io,
    };

    /** @brief Host-policy input describing one bounded resource expectation. */
    struct ModuleResourceBudget {
        std::string id;                                           /**< Canonical module-namespaced budget identity. */
        ModuleResourceBudgetKind kind{};                          /**< Resource category governed by the hint. */
        std::uint64_t limit{};                                    /**< Requested upper bound in the category's native unit. */
        ModuleThreadAffinity affinity{ModuleThreadAffinity::Any}; /**< Execution domain governed by the hint. */
    };

    /** @brief Kind of observability contribution declared by a module. */
    enum class ModuleObservabilityKind : std::uint8_t {
        LogCategory,
        MetricInstrument,
        ProfilerZone,
        DiagnosticBundleHook,
    };

    /** @brief Inert identity of one observability contribution. */
    struct ModuleObservabilityDescriptor {
        ModuleObservabilityKind kind{}; /**< Registry family that owns the contribution. */
        std::string id;                 /**< Canonical module-namespaced contribution identity. */
    };

    /** @brief Host-owned narrow dependency bundle supplied only at activation time. */
    class ModuleActivationContext;

    /** @brief Typed activation callback invoked later by an application composition root. */
    using ModuleActivateCallback = Result<void> (*)(ModuleActivationContext &context) noexcept;

    /** @brief Typed deactivation callback invoked later by an application composition root. */
    using ModuleDeactivateCallback = void (*)(ModuleActivationContext &context) noexcept;

    /** @brief Optional lifecycle entry points named by an inert module descriptor. */
    struct ModuleLifecycleCallbacks {
        ModuleActivateCallback activate{};     /**< Activation entry point; never called during validation. */
        ModuleDeactivateCallback deactivate{}; /**< Matching deactivation entry point. */
    };

    /** @brief Complete inert metadata used to validate one built-in module before composition. */
    struct ModuleDescriptor {
        ModuleId id;                                              /**< Stable module identity. */
        ModuleContractVersion version{};                          /**< Module contract version. */
        std::vector<ModuleDependency> dependencies;               /**< Explicit module dependencies. */
        std::vector<ModuleCapabilityId> providedCapabilities;     /**< Capabilities made available after activation. */
        std::vector<ModuleCapabilityId> requiredCapabilities;     /**< Capabilities required before activation. */
        std::vector<ModuleResourceBudget> resourceBudgets;        /**< Host-policy resource hints. */
        std::vector<ModuleObservabilityDescriptor> observability; /**< Inert observability contributions. */
        ModuleLifecycleCallbacks lifecycle{};                     /**< Optional paired lifecycle callbacks. */
    };

    /** @brief Validated deterministic module order safe to hand to a composition root. */
    struct ValidatedModuleGraph {
        std::vector<ModuleId> initializationOrder; /**< Providers before dependants, with stable tie-breaking. */
    };

    /**
     * @brief Validates inert module descriptors and computes their deterministic dependency order.
     * @param descriptors Complete descriptor set selected by the host for one composition attempt.
     * @return Validated provider-before-dependant order, or a typed failure before any callback runs.
     *
     * Validation rejects malformed or duplicate identities, malformed descriptor-local metadata,
     * missing or version-incompatible required dependencies, missing capabilities, and dependency
     * cycles. Optional dependencies participate in ordering only when their provider is present.
     */
    [[nodiscard]] Result<ValidatedModuleGraph> ValidateModuleGraph(std::span<const ModuleDescriptor> descriptors);
}  // namespace Horo
