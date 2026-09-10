#pragma once

/**
 * @file PCGIdentity.h
 * @brief Durable PCG graph identities and revision-fenced execution/output identities.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"
#include "Horo/PCG/PCGErrors.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Horo::PCG {
    /** @brief Strong persistent non-zero PCG identity in one tag-defined authored domain. */
    template <typename Tag> using PcgStableIdentity = Foundation::Detail::NonZeroId64<Tag, PCGErrors::IdentityInvalid>;

    struct GraphIdentityTag;
    struct NodeIdentityTag;
    struct PinIdentityTag;
    struct GraphRevisionIdentityTag;
    struct ExecutionValueIdentityTag;
    struct SourceSampleIdentityTag;

    /** @brief Stable authored graph identity, independent of path, name, address, and load instance. */
    using GraphId = PcgStableIdentity<GraphIdentityTag>;
    /** @brief Stable authored node identity, independent of array order, display name, and address. */
    using NodeId = PcgStableIdentity<NodeIdentityTag>;
    /** @brief Stable authored pin identity, independent of node layout, label, and address. */
    using PinId = PcgStableIdentity<PinIdentityTag>;
    /** @brief Non-zero durable revision of one graph generation. */
    using GraphRevision = PcgStableIdentity<GraphRevisionIdentityTag>;
    /** @brief Owner-issued durable identity of one execution within an exact graph generation. */
    using ExecutionValue = PcgStableIdentity<ExecutionValueIdentityTag>;
    /** @brief Stable source-sample identity used by deterministic generated-output derivation. */
    using SourceSampleId = PcgStableIdentity<SourceSampleIdentityTag>;

    /** @brief Canonical network-byte-order encoding of one stable PCG identity. */
    using SerializedStableIdentity = std::array<std::uint8_t, sizeof(std::uint64_t)>;
    /** @brief Canonical encoding of graph plus exact durable revision. */
    using SerializedGraphGeneration = std::array<std::uint8_t, 16>;
    /** @brief Canonical encoding of graph generation plus execution value. */
    using SerializedExecutionId = std::array<std::uint8_t, 24>;
    /** @brief Canonical encoding of an exact generated-output identity. */
    using SerializedGeneratedOutputId = std::array<std::uint8_t, 52>;

    /**
     * @brief Encodes one stable PCG identity without process-local state.
     * @param identity Stable identity to encode.
     * @return Exact eight-byte network-order representation; invalid encodes as zero.
     */
    template <typename Tag>
    [[nodiscard]] constexpr SerializedStableIdentity SerializeStableIdentity(const PcgStableIdentity<Tag> identity) noexcept {
        SerializedStableIdentity bytes{};
        std::uint64_t remaining = identity.Value();
        for (auto iterator = bytes.rbegin(); iterator != bytes.rend(); ++iterator) {
            *iterator = static_cast<std::uint8_t>(remaining & 0xffU);
            remaining >>= 8U;
        }
        return bytes;
    }

    /**
     * @brief Decodes one stable PCG identity from canonical bytes.
     * @param bytes Exact network-order representation.
     * @return Typed identity or PCGErrors::SerializedIdentityInvalid for the reserved zero value.
     */
    template <typename Tag> [[nodiscard]] Result<PcgStableIdentity<Tag>> DeserializeStableIdentity(const SerializedStableIdentity &bytes) {
        std::uint64_t value{};
        for (const std::uint8_t byte : bytes)
            value = (value << 8U) | byte;
        auto identity = PcgStableIdentity<Tag>::Create(value);
        if (identity.HasError())
            return Result<PcgStableIdentity<Tag>>::Failure(MakeError(PCGErrors::SerializedIdentityInvalid));
        return identity;
    }

    /** @brief Exact durable graph generation used to fence cook, execution, and output replacement. */
    struct GraphGeneration final {
        GraphId graph{};          /**< Stable authored graph identity. */
        GraphRevision revision{}; /**< Exact non-zero durable source/cooked generation revision. */

        /** @brief Checks representation. @return True when graph and revision are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return graph.IsValid() && revision.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const GraphGeneration &) const noexcept = default;
    };

    /** @brief Durable identity of one execution fenced to an exact graph generation. */
    struct ExecutionId final {
        GraphGeneration generation{}; /**< Exact graph generation being executed. */
        ExecutionValue value{};       /**< Stable owner-issued execution value. */

        /** @brief Checks representation. @return True when every durable component is usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return generation.IsValid() && value.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const ExecutionId &) const noexcept = default;
    };

    /** @brief Durable deterministic generated-output identity in one exact execution. */
    struct GeneratedOutputId final {
        ExecutionId execution{}; /**< Exact graph generation and execution that produced the output. */
        NodeId node{};           /**< Stable output-node identity. */
        PinId pin{};             /**< Stable output-pin identity. */
        SourceSampleId sample{}; /**< Stable input sample identity, never a pointer or container index. */
        std::uint32_t ordinal{}; /**< Deterministic zero-based ordinal within the stable sample. */

        /** @brief Checks representation. @return True when all typed identity components are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return execution.IsValid() && node.IsValid() && pin.IsValid() && sample.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const GeneratedOutputId &) const noexcept = default;
    };

    /** @brief Encodes an exact graph generation. @param generation Generation to persist.
     * @return Canonical network-order bytes; invalid components encode as zero.
     */
    [[nodiscard]] SerializedGraphGeneration SerializeGraphGeneration(GraphGeneration generation) noexcept;
    /** @brief Decodes an exact graph generation. @param bytes Canonical bytes.
     * @return Valid generation or PCGErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<GraphGeneration> DeserializeGraphGeneration(const SerializedGraphGeneration &bytes);

    /** @brief Encodes a revision-fenced execution identity. @param execution Identity to persist.
     * @return Canonical network-order bytes; invalid components encode as zero.
     */
    [[nodiscard]] SerializedExecutionId SerializeExecutionId(ExecutionId execution) noexcept;
    /** @brief Decodes a revision-fenced execution identity. @param bytes Canonical bytes.
     * @return Valid execution or PCGErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<ExecutionId> DeserializeExecutionId(const SerializedExecutionId &bytes);

    /** @brief Encodes a generated-output identity without runtime handles or native values.
     * @param output Exact generated output identity.
     * @return Canonical network-order bytes; invalid components encode as zero.
     */
    [[nodiscard]] SerializedGeneratedOutputId SerializeGeneratedOutputId(const GeneratedOutputId &output) noexcept;
    /** @brief Decodes an exact generated-output identity. @param bytes Canonical bytes.
     * @return Valid output identity or PCGErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<GeneratedOutputId> DeserializeGeneratedOutputId(const SerializedGeneratedOutputId &bytes);

    /**
     * @brief Advances one valid graph revision without wraparound.
     * @param revision Current non-zero durable revision.
     * @return Next revision, IdentityInvalid, or RevisionExhausted.
     */
    [[nodiscard]] Result<GraphRevision> AdvanceGraphRevision(GraphRevision revision);

    /**
     * @brief Validates an execution against the exact current graph generation and execution.
     * @param submitted Consumer-supplied execution identity.
     * @param current Exact current execution identity.
     * @return Success, IdentityInvalid, IdentityUnknown, or IdentityStale.
     */
    [[nodiscard]] Result<void> ValidateExecutionAccess(ExecutionId submitted, ExecutionId current);

    /**
     * @brief Validates generated output against the exact current execution.
     * @param submitted Consumer-supplied output identity.
     * @param currentExecution Exact current execution identity.
     * @return Success, IdentityInvalid, IdentityUnknown, or IdentityStale.
     * @post Success proves generation/execution association only; the output owner still proves residency.
     */
    [[nodiscard]] Result<void> ValidateGeneratedOutputAccess(const GeneratedOutputId &submitted, ExecutionId currentExecution);
}  // namespace Horo::PCG
