#pragma once

/**
 * @file PipelineStepRegistry.h
 * @brief Host-owned pipeline-step extension point with transactional output publication.
 */

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Extensions {
    /** @brief Stable identity of a pipeline step contribution. */
    struct PipelineStepId final {
        std::string value;
        [[nodiscard]] bool operator==(const PipelineStepId &) const noexcept = default;
    };

    /** @brief Stable identity of an input or generated pipeline artifact. */
    struct PipelineArtifactId final {
        std::string value;
        [[nodiscard]] bool operator==(const PipelineArtifactId &) const noexcept = default;
    };

    /** @brief Host-owned pipeline phase; dependencies remain the primary ordering authority. */
    enum class PipelinePhase : std::uint8_t {
        Validate,
        Build,
        Cook,
        Package,
    };

    /** @brief Immutable identity, graph, artifact, and provider ownership declaration for one step. */
    struct PipelineStepDescriptor final {
        PipelineStepId stepId;                     /**< Canonical contribution identity. */
        std::string providerId;                    /**< Canonical module/provider identity. */
        std::uint64_t providerGeneration{};        /**< Non-zero activation generation. */
        PipelinePhase phase{PipelinePhase::Build}; /**< Host phase containing this step. */
        std::vector<PipelineStepId> dependencies;  /**< Sorted unique prerequisite step identities. */
        std::vector<PipelineArtifactId> inputs;    /**< Sorted unique required artifact identities. */
        std::vector<PipelineArtifactId> outputs;   /**< Sorted unique generated artifact identities. */
    };

    /** @brief Immutable borrowed artifact available for one synchronous pipeline run. */
    struct PipelineArtifactView final {
        std::string_view id;              /**< Canonical identity borrowed for this synchronous call. */
        std::span<const std::byte> bytes; /**< Immutable bytes borrowed for this synchronous call. */
    };

    /** @brief Host-owned artifact value published only after the complete graph succeeds. */
    struct PipelineArtifact final {
        PipelineArtifactId id;
        std::vector<std::byte> bytes;
    };

    /** @brief Immutable artifact snapshot exposed to one step invocation. */
    class PipelineStepContext final {
    public:
        /**
         * @brief Finds an artifact declared as an input by the current step.
         * @param id Exact artifact identity.
         * @return Borrowed bytes valid only for the current callback, or null when unavailable.
         */
        [[nodiscard]] const PipelineArtifactView *Find(const PipelineArtifactId &id) const noexcept;

    private:
        friend class PipelineStepRegistry;
        explicit PipelineStepContext(std::span<const PipelineArtifactView> artifacts) noexcept;
        std::span<const PipelineArtifactView> artifacts_;
    };

    /** @brief Bounded transaction-local output sink; providers cannot publish outside their declaration. */
    class PipelineOutputSink final {
    public:
        /**
         * @brief Stages one declared output for publication after the whole graph succeeds.
         * @param id Exact output identity declared by the current step.
         * @param bytes Immutable output payload copied into host-owned staging memory.
         * @return Success or a typed undeclared, duplicate, or capacity failure.
         */
        [[nodiscard]] Result<void> Write(const PipelineArtifactId &id, std::span<const std::byte> bytes);

    private:
        friend class PipelineStepRegistry;
        PipelineOutputSink(std::span<const PipelineArtifactId> declaredOutputs, std::uint64_t maximumBytes) noexcept;
        [[nodiscard]] Result<std::vector<PipelineArtifact>> Complete();

        std::span<const PipelineArtifactId> declaredOutputs_;
        std::vector<PipelineArtifact> outputs_;
        std::uint64_t maximumBytes_{};
        std::uint64_t writtenBytes_{};
    };

    /** @brief Trusted synchronous backend step invoked through host-owned inputs and output staging. */
    class IPipelineStep {
    public:
        virtual ~IPipelineStep() = default;

        /**
         * @brief Executes one step without directly publishing generated artifacts.
         * @param context Immutable artifacts available at this point in the dependency graph.
         * @param outputs Transaction-local sink accepting only this step's declared outputs.
         * @param cancellation Cooperative cancellation observed by provider work.
         * @return Success after producing every declared output, or a typed provider failure.
         */
        [[nodiscard]] virtual Result<void> Execute(const PipelineStepContext &context, PipelineOutputSink &outputs,
                                                   const CancellationToken &cancellation) const = 0;
    };

    struct PipelineStepRegistryState;
    struct PipelineStepProviderState;

    /** @brief Move-only publication whose lifetime controls future pipeline admission. */
    class PipelineStepRegistration final {
    public:
        ~PipelineStepRegistration() noexcept;
        PipelineStepRegistration(const PipelineStepRegistration &) = delete;
        PipelineStepRegistration &operator=(const PipelineStepRegistration &) = delete;
        PipelineStepRegistration(PipelineStepRegistration &&other) noexcept;
        PipelineStepRegistration &operator=(PipelineStepRegistration &&other);

        /** @brief Idempotently removes this exact provider generation from future runs. */
        void Reset();
        /** @brief Reports whether the publication remains discoverable for new runs. */
        [[nodiscard]] bool IsRegistered() const noexcept;

    private:
        friend class PipelineStepRegistry;
        PipelineStepRegistration(std::weak_ptr<PipelineStepRegistryState> registry,
                                 std::shared_ptr<PipelineStepProviderState> provider) noexcept;

        std::weak_ptr<PipelineStepRegistryState> registry_;
        std::shared_ptr<PipelineStepProviderState> provider_;
    };

    /** @brief Complete successful pipeline result with deterministic order and generated artifacts. */
    struct PipelineRunResult final {
        std::vector<PipelineStepId> executionOrder;
        std::vector<PipelineArtifact> outputs;
    };

    /** @brief Explicit composition-owned registry and transactional synchronous pipeline dispatcher. */
    class PipelineStepRegistry final {
    public:
        static constexpr std::size_t MaximumProviders = 256;               /**< Hard publication bound. */
        static constexpr std::size_t MaximumInitialArtifacts = 65536;      /**< Hard input count bound per run. */
        static constexpr std::size_t MaximumArtifactsPerStep = 4096;       /**< Hard declared input/output bound per step. */
        static constexpr std::uint64_t MaximumArtifactBytes = 1ULL << 34U; /**< Hard aggregate byte bound per run. */

        PipelineStepRegistry();
        ~PipelineStepRegistry() noexcept;
        PipelineStepRegistry(const PipelineStepRegistry &) = delete;
        PipelineStepRegistry &operator=(const PipelineStepRegistry &) = delete;
        PipelineStepRegistry(PipelineStepRegistry &&) noexcept = default;
        PipelineStepRegistry &operator=(PipelineStepRegistry &&other);

        /**
         * @brief Publishes inert step metadata and implementation from the composition root.
         * @param descriptor Exact provider, graph, input, and output declaration.
         * @param provider Shared provider retained by already-admitted synchronous runs.
         * @return Lifetime registration or a typed invalid, duplicate, capacity, or shutdown failure.
         * @note Registration never invokes provider code.
         */
        [[nodiscard]] Result<PipelineStepRegistration> Register(PipelineStepDescriptor descriptor,
                                                                std::shared_ptr<const IPipelineStep> provider);

        /**
         * @brief Executes the complete registered dependency graph transactionally.
         * @param initialArtifacts Sorted unique host-owned input views valid until this call returns.
         * @param cancellation Cooperative cancellation checked around every provider callback.
         * @return Complete deterministic order and generated outputs, or failure with no partial publication.
         */
        [[nodiscard]] Result<PipelineRunResult> Execute(std::span<const PipelineArtifactView> initialArtifacts,
                                                        const CancellationToken &cancellation) const;

        /** @brief Idempotently closes publication and new run admission. */
        void BeginShutdown();
        /** @brief Reports whether the registry has entered terminal shutdown. */
        [[nodiscard]] bool IsShutdown() const;

    private:
        /** @brief Invokes one admitted provider against pre-resolved declared inputs. */
        [[nodiscard]] static Result<std::vector<PipelineArtifact>> InvokeStep(const std::shared_ptr<PipelineStepProviderState> &provider,
                                                                              std::span<const PipelineArtifactView> inputs,
                                                                              std::uint64_t remainingBytes,
                                                                              const CancellationToken &cancellation);
        std::shared_ptr<PipelineStepRegistryState> state_;
    };
}  // namespace Horo::Extensions
