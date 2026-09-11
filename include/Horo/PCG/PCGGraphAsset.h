#pragma once

/**
 * @file PCGGraphAsset.h
 * @brief Versioned bounded PCG graph source, canonical encoding, and migration boundary.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/PCG/PCGIdentity.h"
#include "Horo/PCG/PCGPointSchema.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Horo::PCG {
    /** @brief Persisted PCG graph-source schema version. */
    struct PCGGraphSchemaVersion final {
        std::uint16_t major{1};
        std::uint16_t minor{1};
        [[nodiscard]] constexpr auto operator<=>(const PCGGraphSchemaVersion &) const noexcept = default;
    };

    /** @brief Current schema; version 1.0 is admitted only through an explicit migrator. */
    inline constexpr PCGGraphSchemaVersion CurrentPCGGraphSchemaVersion{1, 1};

    /** @brief Compatibility classification made before allocating graph containers. */
    enum class PCGGraphSchemaCompatibility : std::uint8_t {
        Exact,
        MigrationRequired,
        Unsupported
    };

    /** @brief Compile-time ceilings that untrusted graph source cannot raise. */
    struct PCGGraphSourceHardLimits final {
        static constexpr std::size_t SourceBytes = 64U * 1024U * 1024U;
        static constexpr std::size_t IdentifierBytes = 96;
        static constexpr std::size_t PinsPerNode = 128;
        static constexpr std::size_t TotalPins = 131'072;
        static constexpr std::size_t NodePayloadBytes = 1U * 1024U * 1024U;
        static constexpr std::size_t NodeTypes = 4'096;
    };

    /** @brief Project-lowerable limits applied before source allocation or publication. */
    struct PCGGraphSourceLimits final {
        std::size_t maximumSourceBytes{};                                                /**< Maximum complete encoded source size. */
        std::size_t maximumNodes{};                                                      /**< Maximum authored node count. */
        std::size_t maximumEdges{};                                                      /**< Maximum authored edge count. */
        std::size_t maximumExposedInputs{};                                              /**< Maximum externally bound input count. */
        std::size_t maximumIdentifierBytes{PCGGraphSourceHardLimits::IdentifierBytes};   /**< Maximum canonical key byte count. */
        std::size_t maximumPinsPerNode{PCGGraphSourceHardLimits::PinsPerNode};           /**< Maximum pins owned by one node. */
        std::size_t maximumTotalPins{PCGGraphSourceHardLimits::TotalPins};               /**< Maximum pins across the complete graph. */
        std::size_t maximumNodePayloadBytes{PCGGraphSourceHardLimits::NodePayloadBytes}; /**< Maximum opaque bytes per node. */
        [[nodiscard]] constexpr auto operator<=>(const PCGGraphSourceLimits &) const noexcept = default;
    };

    /**
     * @brief Returns graph-source ceilings derived from one exact ADR-156 operational tier.
     * @param tier Operational tier whose immutable ceilings are requested.
     * @return Bounded graph-source limits or GraphSourceCapacityExceeded for an unknown tier.
     */
    [[nodiscard]] Result<PCGGraphSourceLimits> GraphSourceLimitsForTier(PCGOperationalTier tier);

    /** @brief Closed authoring/runtime intent persisted without granting execution authority. */
    enum class PCGGenerationMode : std::uint8_t {
        Offline,
        Runtime,
        Hybrid,
        Count
    };

    /** @brief Exact authored semantic version of one node type. */
    struct PCGNodeTypeVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return major != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const PCGNodeTypeVersion &) const noexcept = default;
    };

    /** @brief Typed graph pin direction. */
    enum class PCGPinDirection : std::uint8_t {
        Input,
        Output,
        Count
    };

    /** @brief Closed schema-1 graph value and data-flow vocabulary. */
    enum class PCGPinType : std::uint8_t {
        PointSet,
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Scalar,
        Vector2,
        Vector3,
        Vector4,
        Count
    };

    /** @brief Incoming edge cardinality; output pins must use Multiple. */
    enum class PCGPinCardinality : std::uint8_t {
        Single,
        Multiple,
        Count
    };

    /** @brief Closed typed persisted value; monostate represents no value and is never a valid default. */
    using PCGGraphValue = std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, Math::Vec2, Math::Vec3, Math::Vec4>;

    /** @brief Stable typed pin carried by one authored node. */
    struct PCGGraphPin final {
        PinId id{};                                               /**< Stable pin identity, globally unique within the graph. */
        PCGPinDirection direction{PCGPinDirection::Input};        /**< Direction of data flow. */
        PCGPinType type{PCGPinType::PointSet};                    /**< Exact data type accepted or produced. */
        PCGPinCardinality cardinality{PCGPinCardinality::Single}; /**< Permitted incoming edge cardinality. */
        std::optional<PCGGraphValue> defaultValue{};              /**< Optional typed input default; outputs never carry one. */
        [[nodiscard]] bool operator==(const PCGGraphPin &) const noexcept = default;
    };

    /** @brief One authored node with a bounded opaque semantic payload owned by its node type. */
    struct PCGGraphNode final {
        NodeId id{};                         /**< Stable authored node identity. */
        NodeTypeId type{};                   /**< Semantic type resolved only through the supplied catalog snapshot. */
        PCGNodeTypeVersion version{};        /**< Exact authored semantic type version. */
        std::vector<PCGGraphPin> pins{};     /**< Bounded pin declarations canonicalized by identity. */
        std::vector<std::uint8_t> payload{}; /**< Bounded opaque semantic payload owned by the node type. */
        [[nodiscard]] bool operator==(const PCGGraphNode &) const noexcept = default;
    };

    /** @brief Stable directed edge between an output pin and an input pin. */
    struct PCGGraphEdge final {
        EdgeId id{};         /**< Stable authored edge identity. */
        NodeId sourceNode{}; /**< Node owning the output endpoint. */
        PinId sourcePin{};   /**< Output endpoint identity. */
        NodeId targetNode{}; /**< Node owning the input endpoint. */
        PinId targetPin{};   /**< Input endpoint identity. */
        [[nodiscard]] constexpr auto operator<=>(const PCGGraphEdge &) const noexcept = default;
    };

    /** @brief One stable externally supplied value bound to an exact input pin. */
    struct PCGExposedInput final {
        ExposedInputId id{};          /**< Stable authored exposed-input identity. */
        std::string key{};            /**< Canonical namespaced external key. */
        NodeId node{};                /**< Node owning the bound input. */
        PinId pin{};                  /**< Exact bound input pin. */
        PCGGraphValue defaultValue{}; /**< Required finite typed default. */
        [[nodiscard]] bool operator==(const PCGExposedInput &) const noexcept = default;
    };

    /** @brief Complete mutable source carrier validated and canonicalized transactionally. */
    struct PCGGraphSourceData final {
        PCGGraphSchemaVersion version{CurrentPCGGraphSchemaVersion}; /**< Persisted schema version. */
        GraphGeneration generation{};                                /**< Stable graph identity and exact source revision. */
        PCGOperationalTier tier{PCGOperationalTier::Baseline};       /**< Required operational tier. */
        PCGGenerationMode mode{PCGGenerationMode::Offline};          /**< Closed authoring/runtime generation intent. */
        std::uint64_t deterministicSeed{};                           /**< Stable seed interpreted by the cook contract. */
        std::vector<PCGGraphNode> nodes{};                           /**< Canonically ordered node source. */
        std::vector<PCGGraphEdge> edges{};                           /**< Canonically ordered directed edges. */
        std::vector<PCGExposedInput> exposedInputs{};                /**< Canonically ordered external bindings. */
        [[nodiscard]] bool operator==(const PCGGraphSourceData &) const noexcept = default;
    };

    /** @brief Exact node-type interval captured from an immutable host-composed catalog snapshot. */
    struct PCGNodeTypeSupport final {
        NodeTypeId type{};                   /**< Stable semantic type identity. */
        PCGNodeTypeVersion minimumVersion{}; /**< Oldest accepted authored version, inclusive. */
        PCGNodeTypeVersion maximumVersion{}; /**< Newest accepted authored version, inclusive. */
        [[nodiscard]] auto operator<=>(const PCGNodeTypeSupport &) const noexcept = default;
    };

    /** @brief Policy for unavailable node semantics at the authoring/source boundary. */
    enum class PCGUnknownNodePolicy : std::uint8_t {
        Reject,
        PreserveInert,
        Count
    };

    /** @brief Owner lifecycle captured before parsing, migration, or validation. */
    enum class PCGGraphSourceAdmissionState : std::uint8_t {
        Accepting,
        CancellationRequested,
        ShuttingDown,
        Count
    };

    /** @brief Complete explicit load context; catalog storage must outlive the call only. */
    struct PCGGraphSourceContext final {
        PCGOperationalTier tier{PCGOperationalTier::Baseline};                           /**< Exact project/runtime tier. */
        PCGUnknownNodePolicy unknownNodePolicy{PCGUnknownNodePolicy::Reject};            /**< Unknown semantic handling policy. */
        PCGGraphSourceAdmissionState admission{PCGGraphSourceAdmissionState::Accepting}; /**< Captured lifecycle gate. */
        PCGGraphSourceLimits limits{};                            /**< Optional project-lowered limits; all-zero selects tier defaults. */
        std::span<const PCGNodeTypeSupport> supportedNodeTypes{}; /**< Immutable catalog projection valid for the call. */
    };

    /** @brief Immutable canonical graph source safe for concurrent read-only use. */
    class PCGGraphAsset final {
    public:
        PCGGraphAsset() = delete;

        /**
         * @brief Validates, canonicalizes, and owns one detached graph source.
         * @param candidate Complete candidate; moved only after validation succeeds.
         * @param context Exact tier, limits, catalog snapshot, lifecycle, and unknown-node policy.
         * @return Immutable source or a typed version, malformed, duplicate, topology, capacity, unknown, or lifecycle failure.
         * @post Failure publishes no partial graph and mutates no existing asset.
         */
        [[nodiscard]] static Result<PCGGraphAsset> Create(PCGGraphSourceData candidate, const PCGGraphSourceContext &context);

        /** @brief Returns immutable canonical source data. @return Owned source data valid for this asset's lifetime. */
        [[nodiscard]] const PCGGraphSourceData &Data() const noexcept;
        /** @brief Returns unavailable node count retained only for inert authoring round trips. @return Unavailable node count. */
        [[nodiscard]] std::size_t UnknownNodeCount() const noexcept;
        /** @brief Returns whether every node has exact catalog support and the source may proceed to cook validation. @return True when
         * cook-eligible. */
        [[nodiscard]] bool IsCookEligible() const noexcept;

    private:
        PCGGraphAsset(PCGGraphSourceData data, std::size_t unknownNodeCount) noexcept;
        PCGGraphSourceData data_;
        std::size_t unknownNodeCount_{};
    };

    /** @brief Explicit bounded source migration hook composed by the authoring host, never discovered globally. */
    class IPCGGraphSourceMigrator {
    public:
        virtual ~IPCGGraphSourceMigrator() = default;

        /**
         * @brief Migrates one complete older canonical source to the requested schema.
         * @param source Immutable older source bytes.
         * @param from Parsed older schema version.
         * @param to Exact requested current schema.
         * @param maximumOutputBytes Hard output ceiling that the implementation must honor.
         * @return Complete migrated bytes or a typed migration failure.
         * @note Load/control path only; implementations must be deterministic and perform no publication.
         */
        [[nodiscard]] virtual Result<std::vector<std::uint8_t>> Migrate(std::span<const std::uint8_t> source, PCGGraphSchemaVersion from,
                                                                        PCGGraphSchemaVersion to, std::size_t maximumOutputBytes) const = 0;
    };

    /**
     * @brief Classifies one persisted graph-source schema.
     * @param version Persisted schema to classify.
     * @return Exact, MigrationRequired, or Unsupported without inspecting payload data.
     */
    [[nodiscard]] constexpr PCGGraphSchemaCompatibility ClassifyPCGGraphSchemaCompatibility(const PCGGraphSchemaVersion version) noexcept {
        if (version == CurrentPCGGraphSchemaVersion)
            return PCGGraphSchemaCompatibility::Exact;
        if (version.major == CurrentPCGGraphSchemaVersion.major && version.minor < CurrentPCGGraphSchemaVersion.minor)
            return PCGGraphSchemaCompatibility::MigrationRequired;
        return PCGGraphSchemaCompatibility::Unsupported;
    }

    /**
     * @brief Reads only the fixed source envelope version without allocating graph containers.
     * @param source Complete or partial untrusted source envelope.
     * @return Parsed version or GraphSourceMalformed.
     */
    [[nodiscard]] Result<PCGGraphSchemaVersion> InspectPCGGraphSchemaVersion(std::span<const std::uint8_t> source);

    /**
     * @brief Emits the exact canonical network-order source representation.
     * @param asset Valid immutable graph source to encode.
     * @param maximumOutputBytes Caller-selected output ceiling.
     * @return Canonical bytes or GraphSourceCapacityExceeded.
     */
    [[nodiscard]] Result<std::vector<std::uint8_t>> SerializePCGGraphAsset(
        const PCGGraphAsset &asset, std::size_t maximumOutputBytes = PCGGraphSourceHardLimits::SourceBytes);

    /**
     * @brief Parses, optionally migrates, and transactionally validates an untrusted graph source.
     * @param source Complete source bytes.
     * @param context Exact load context and finite limits.
     * @param migrator Optional explicit hook used only when compatibility is MigrationRequired.
     * @return Immutable canonical graph or a typed malformed, migration, topology, catalog, capacity, or lifecycle failure.
     */
    [[nodiscard]] Result<PCGGraphAsset> DeserializePCGGraphAsset(std::span<const std::uint8_t> source, const PCGGraphSourceContext &context,
                                                                 const IPCGGraphSourceMigrator *migrator = nullptr);

    /**
     * @brief Validates a detached replacement against exact graph lineage before returning a new immutable root.
     * @param current Currently published immutable source.
     * @param candidate Detached replacement candidate.
     * @param context Exact replacement validation context.
     * @return New source or a typed validation/replacement failure; current remains unchanged.
     */
    [[nodiscard]] Result<PCGGraphAsset> ReplacePCGGraphAsset(const PCGGraphAsset &current, PCGGraphSourceData candidate,
                                                             const PCGGraphSourceContext &context);
}  // namespace Horo::PCG
