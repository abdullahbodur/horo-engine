#include "Horo/PCG/PCGIdentity.h"

namespace Horo::PCG {
    namespace {
        template <std::size_t Size>
        void WriteNetworkValue(std::array<std::uint8_t, Size> &bytes, const std::size_t offset, const std::uint64_t value,
                               const std::size_t width) noexcept {
            for (std::size_t byte = 0; byte < width; ++byte) {
                const std::size_t shift = (width - byte - 1U) * 8U;
                bytes[offset + byte] = static_cast<std::uint8_t>(value >> shift);
            }
        }

        template <std::size_t Size>
        [[nodiscard]] std::uint64_t ReadNetworkValue(const std::array<std::uint8_t, Size> &bytes, const std::size_t offset,
                                                     const std::size_t width) noexcept {
            std::uint64_t value{};
            for (std::size_t byte = 0; byte < width; ++byte)
                value = (value << 8U) | bytes[offset + byte];
            return value;
        }

        template <typename Identity> Result<Identity> DecodeIdentity(const std::uint64_t value) {
            auto decoded = Identity::Create(value);
            if (decoded.HasError())
                return Result<Identity>::Failure(MakeError(PCGErrors::SerializedIdentityInvalid));
            return decoded;
        }
    }  // namespace

    /** @copydoc SerializeGraphGeneration */
    SerializedGraphGeneration SerializeGraphGeneration(const GraphGeneration generation) noexcept {
        SerializedGraphGeneration bytes{};
        WriteNetworkValue(bytes, 0, generation.graph.Value(), 8);
        WriteNetworkValue(bytes, 8, generation.revision.Value(), 8);
        return bytes;
    }

    /** @copydoc DeserializeGraphGeneration */
    Result<GraphGeneration> DeserializeGraphGeneration(const SerializedGraphGeneration &bytes) {
        auto graph = DecodeIdentity<GraphId>(ReadNetworkValue(bytes, 0, 8));
        auto revision = DecodeIdentity<GraphRevision>(ReadNetworkValue(bytes, 8, 8));
        if (graph.HasError() || revision.HasError())
            return Result<GraphGeneration>::Failure(MakeError(PCGErrors::SerializedIdentityInvalid));
        return Result<GraphGeneration>::Success({graph.Value(), revision.Value()});
    }

    /** @copydoc SerializeExecutionId */
    SerializedExecutionId SerializeExecutionId(const ExecutionId execution) noexcept {
        SerializedExecutionId bytes{};
        WriteNetworkValue(bytes, 0, execution.generation.graph.Value(), 8);
        WriteNetworkValue(bytes, 8, execution.generation.revision.Value(), 8);
        WriteNetworkValue(bytes, 16, execution.value.Value(), 8);
        return bytes;
    }

    /** @copydoc DeserializeExecutionId */
    Result<ExecutionId> DeserializeExecutionId(const SerializedExecutionId &bytes) {
        auto graph = DecodeIdentity<GraphId>(ReadNetworkValue(bytes, 0, 8));
        auto revision = DecodeIdentity<GraphRevision>(ReadNetworkValue(bytes, 8, 8));
        auto value = DecodeIdentity<ExecutionValue>(ReadNetworkValue(bytes, 16, 8));
        if (graph.HasError() || revision.HasError() || value.HasError())
            return Result<ExecutionId>::Failure(MakeError(PCGErrors::SerializedIdentityInvalid));
        return Result<ExecutionId>::Success({{graph.Value(), revision.Value()}, value.Value()});
    }

    /** @copydoc SerializeGeneratedOutputId */
    SerializedGeneratedOutputId SerializeGeneratedOutputId(const GeneratedOutputId &output) noexcept {
        SerializedGeneratedOutputId bytes{};
        WriteNetworkValue(bytes, 0, output.execution.generation.graph.Value(), 8);
        WriteNetworkValue(bytes, 8, output.execution.generation.revision.Value(), 8);
        WriteNetworkValue(bytes, 16, output.execution.value.Value(), 8);
        WriteNetworkValue(bytes, 24, output.node.Value(), 8);
        WriteNetworkValue(bytes, 32, output.pin.Value(), 8);
        WriteNetworkValue(bytes, 40, output.sample.Value(), 8);
        WriteNetworkValue(bytes, 48, output.ordinal, 4);
        return bytes;
    }

    /** @copydoc DeserializeGeneratedOutputId */
    Result<GeneratedOutputId> DeserializeGeneratedOutputId(const SerializedGeneratedOutputId &bytes) {
        auto graph = DecodeIdentity<GraphId>(ReadNetworkValue(bytes, 0, 8));
        auto revision = DecodeIdentity<GraphRevision>(ReadNetworkValue(bytes, 8, 8));
        auto execution = DecodeIdentity<ExecutionValue>(ReadNetworkValue(bytes, 16, 8));
        auto node = DecodeIdentity<NodeId>(ReadNetworkValue(bytes, 24, 8));
        auto pin = DecodeIdentity<PinId>(ReadNetworkValue(bytes, 32, 8));
        auto sample = DecodeIdentity<SourceSampleId>(ReadNetworkValue(bytes, 40, 8));
        if (graph.HasError() || revision.HasError() || execution.HasError() || node.HasError() || pin.HasError() || sample.HasError())
            return Result<GeneratedOutputId>::Failure(MakeError(PCGErrors::SerializedIdentityInvalid));
        return Result<GeneratedOutputId>::Success({{{graph.Value(), revision.Value()}, execution.Value()},
                                                   node.Value(),
                                                   pin.Value(),
                                                   sample.Value(),
                                                   static_cast<std::uint32_t>(ReadNetworkValue(bytes, 48, 4))});
    }

    /** @copydoc AdvanceGraphRevision */
    Result<GraphRevision> AdvanceGraphRevision(const GraphRevision revision) {
        if (!revision.IsValid())
            return Result<GraphRevision>::Failure(MakeError(PCGErrors::IdentityInvalid));
        if (revision.Value() == std::numeric_limits<std::uint64_t>::max())
            return Result<GraphRevision>::Failure(MakeError(PCGErrors::RevisionExhausted));
        return GraphRevision::Create(revision.Value() + 1U);
    }

    /** @copydoc ValidateExecutionAccess */
    Result<void> ValidateExecutionAccess(const ExecutionId submitted, const ExecutionId current) {
        if (!submitted.IsValid() || !current.IsValid())
            return Result<void>::Failure(MakeError(PCGErrors::IdentityInvalid));
        if (submitted.generation.graph != current.generation.graph)
            return Result<void>::Failure(MakeError(PCGErrors::IdentityUnknown));
        if (submitted.generation.revision != current.generation.revision || submitted.value != current.value)
            return Result<void>::Failure(MakeError(PCGErrors::IdentityStale));
        return Result<void>::Success();
    }

    /** @copydoc ValidateGeneratedOutputAccess */
    Result<void> ValidateGeneratedOutputAccess(const GeneratedOutputId &submitted, const ExecutionId currentExecution) {
        if (!submitted.IsValid() || !currentExecution.IsValid())
            return Result<void>::Failure(MakeError(PCGErrors::IdentityInvalid));
        return ValidateExecutionAccess(submitted.execution, currentExecution);
    }
}  // namespace Horo::PCG
