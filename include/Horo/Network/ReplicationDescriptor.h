#pragma once

/**
 * @file ReplicationDescriptor.h
 * @brief Inert versioned replication schema and stable field declarations.
 */

#include "Horo/Foundation/ModuleDescriptor.h"
#include "Horo/Network/ReplicationIdentity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Horo::Network {
    /** @brief Recipient-selection policy; it never grants state authority. */
    enum class ReplicationCondition : std::uint8_t {
        Always,        /**< Consider for every eligible snapshot. */
        InitialOnly,   /**< Include only in authoritative spawn state. */
        OwnerOnly,     /**< Route only to the granted autonomous client. */
        SkipOwner,     /**< Route to replicas other than the autonomous owner. */
        SimulatedOnly, /**< Route only to simulated-client views. */
        Count          /**< Closed-set sentinel; never a valid policy. */
    };

    /** @brief Whether compatibility may omit a field and substitute its canonical default. */
    enum class ReplicationFieldRequirement : std::uint8_t {
        Required, /**< Missing field rejects the compatible record. */
        Optional, /**< Missing field resolves to the canonical default. */
        Count     /**< Closed-set sentinel; never a valid requirement. */
    };

    /** @brief Canonical fields originate only from the authority server and mutate replicas through owner adapters. */
    enum class ReplicationWritePolicy : std::uint8_t {
        AuthorityServerOnly, /**< Only server-owned canonical capture may originate the field. */
        Count                /**< Closed-set sentinel; never a valid policy. */
    };

    /** @brief Finite per-field decode and allocation envelope in canonical wire units. */
    struct ReplicationFieldLimits final {
        std::uint32_t maximumEncodedBytes{}; /**< Inclusive canonical encoded byte limit. */
        std::uint32_t maximumElementCount{}; /**< Inclusive decoded scalar/container element limit. */

        constexpr auto operator<=>(const ReplicationFieldLimits &) const noexcept = default;
    };

    /** @brief Canonical codec bytes used only when an explicitly optional field is absent. */
    struct ReplicationFieldDefault final {
        std::vector<std::byte> canonicalBytes; /**< Owned codec bytes; an empty canonical encoding is permitted. */

        bool operator==(const ReplicationFieldDefault &) const = default;
    };

    /**
     * @brief One stable field declaration independent of display and C++ storage details.
     *
     * The descriptor intentionally carries no name, property path, member offset, array position,
     * pointer, callback, or native layout. `canonicalDefault` is codec-owned wire data, not a
     * borrowed gameplay value or raw object representation.
     */
    struct ReplicationFieldDescriptor final {
        FieldId id;                                                   /**< Stable wire identity scoped by its schema. */
        ReplicationValueTypeId valueType;                             /**< Registered semantic type identity. */
        ReplicationCodecId codec;                                     /**< Registered canonical bounded codec identity. */
        ReplicationSchemaVersion introducedVersion;                   /**< First schema version publishing this identity. */
        ReplicationCondition condition{ReplicationCondition::Always}; /**< Recipient-selection policy. */
        ReplicationFieldRequirement requirement{ReplicationFieldRequirement::Required};  /**< Compatibility requiredness. */
        ReplicationWritePolicy writePolicy{ReplicationWritePolicy::AuthorityServerOnly}; /**< State-origin authority. */
        ReplicationFieldLimits limits;                                                   /**< Finite decode and allocation envelope. */
        std::optional<ReplicationFieldDefault> canonicalDefault;                         /**< Required for every optional field. */

        bool operator==(const ReplicationFieldDescriptor &) const = default;
    };

    /** @brief Complete inert schema contribution copied into an immutable registry snapshot. */
    struct ReplicationSchemaDescriptor final {
        ReplicationSchemaId id;                         /**< Globally stable schema identity. */
        ReplicationSchemaVersion version;               /**< Current published semantic version. */
        ReplicationCompatibilityRange compatibility;    /**< Explicit same-major accepted interval. */
        ModuleId owner;                                 /**< Exact declaring gameplay/module owner. */
        std::vector<ReplicationFieldDescriptor> fields; /**< Current fields; snapshot construction sorts by identity. */
        std::vector<FieldId> tombstonedFields;          /**< Removed identities that can never be reused. */

        bool operator==(const ReplicationSchemaDescriptor &) const = default;
    };

    /** @brief Explicit finite envelope used while validating and owning one complete schema set. */
    struct ReplicationDescriptorLimits final {
        std::size_t maximumSchemas{};              /**< Complete schema-count limit. */
        std::size_t maximumFieldsPerSchema{};      /**< Combined current-field and tombstone limit per schema. */
        std::size_t maximumOwnerIdentityBytes{};   /**< Maximum canonical module-owner identity bytes. */
        std::size_t maximumDefaultBytesPerField{}; /**< Per-field canonical-default byte limit. */
        std::size_t maximumTotalDefaultBytes{};    /**< Aggregate canonical-default byte limit. */
    };

    /**
     * @brief Validates one descriptor against an explicit finite construction envelope.
     * @param descriptor Inert candidate; validation performs no registration or callback.
     * @param limits Finite host-provided descriptor bounds.
     * @return Success or a stable malformed, conflict, or capacity error.
     */
    [[nodiscard]] Result<void> ValidateReplicationSchemaDescriptor(const ReplicationSchemaDescriptor &descriptor,
                                                                   const ReplicationDescriptorLimits &limits);
}  // namespace Horo::Network
