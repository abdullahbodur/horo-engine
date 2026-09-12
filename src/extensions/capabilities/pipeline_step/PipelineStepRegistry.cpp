#include "Horo/Extensions/PipelineStepRegistry.h"

#include "../../ExtensionAuthorityIdentityValidation.h"
#include "Horo/Extensions/ExtensionErrors.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

namespace Horo::Extensions {
    struct PipelineStepProviderState final {
        PipelineStepDescriptor descriptor;
        std::shared_ptr<const IPipelineStep> provider;
        std::atomic_bool registered{true};
    };

    struct PipelineStepRegistryState final {
        // The mutex protects publication ordering, removal, and shutdown admission.
        // Execute copies strong provider leases before callbacks; reset and shutdown
        // revoke future admission while an already-admitted synchronous run drains.
        std::mutex mutex;
        std::vector<std::shared_ptr<PipelineStepProviderState>> providers;
        bool shutdown{};
    };

    namespace {
        template <typename Id> [[nodiscard]] bool SortedUniqueCanonical(const std::vector<Id> &ids) noexcept {
            std::string_view previous;
            for (const Id &id : ids) {
                if (!Detail::IsCanonicalExtensionAuthorityId(id.value) || (!previous.empty() && id.value <= previous))
                    return false;
                previous = id.value;
            }
            return true;
        }

        [[nodiscard]] bool ValidPhase(const PipelinePhase phase) noexcept {
            return phase >= PipelinePhase::Validate && phase <= PipelinePhase::Package;
        }

        [[nodiscard]] bool ValidDescriptor(const PipelineStepDescriptor &descriptor) noexcept {
            if (!Detail::IsCanonicalExtensionAuthorityId(descriptor.stepId.value) ||
                !Detail::IsCanonicalExtensionAuthorityId(descriptor.providerId) || descriptor.providerGeneration == 0U ||
                !ValidPhase(descriptor.phase) || !SortedUniqueCanonical(descriptor.dependencies) ||
                !SortedUniqueCanonical(descriptor.inputs) || !SortedUniqueCanonical(descriptor.outputs) ||
                descriptor.dependencies.size() > PipelineStepRegistry::MaximumProviders ||
                descriptor.inputs.size() > PipelineStepRegistry::MaximumArtifactsPerStep ||
                descriptor.outputs.size() > PipelineStepRegistry::MaximumArtifactsPerStep)
                return false;
            if (std::ranges::any_of(descriptor.dependencies, [&descriptor](const PipelineStepId &dependency) {
                return dependency == descriptor.stepId;
            }))
                return false;
            return std::ranges::none_of(descriptor.inputs, [&descriptor](const PipelineArtifactId &input) {
                return std::ranges::binary_search(descriptor.outputs, input.value, {}, &PipelineArtifactId::value);
            });
        }

        [[nodiscard]] Result<void> ValidateInitialArtifacts(const std::span<const PipelineArtifactView> artifacts) {
            if (artifacts.size() > PipelineStepRegistry::MaximumInitialArtifacts)
                return Result<void>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryInvalid));
            std::string_view previous;
            std::uint64_t totalBytes = 0U;
            for (const PipelineArtifactView &artifact : artifacts) {
                if (!Detail::IsCanonicalExtensionAuthorityId(artifact.id.value) || (!previous.empty() && artifact.id.value <= previous) ||
                    artifact.bytes.size() > PipelineStepRegistry::MaximumArtifactBytes - totalBytes)
                    return Result<void>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryInvalid));
                previous = artifact.id.value;
                totalBytes += artifact.bytes.size();
            }
            return Result<void>::Success();
        }

        void RemoveProvider(const std::shared_ptr<PipelineStepRegistryState> &registry,
                            const std::shared_ptr<PipelineStepProviderState> &provider) {
            std::scoped_lock lock{registry->mutex};
            provider->registered.store(false, std::memory_order_release);
            std::erase(registry->providers, provider);
        }

        [[nodiscard]] std::optional<std::vector<std::shared_ptr<PipelineStepProviderState>>> SnapshotProviders(
            const std::shared_ptr<PipelineStepRegistryState> &state) {
            std::scoped_lock lock{state->mutex};
            if (state->shutdown)
                return std::nullopt;
            return state->providers;
        }

        [[nodiscard]] Error InvocationFailure(const PipelineStepDescriptor &descriptor, Error cause = {}) {
            const std::string detail = std::format("Pipeline step failed: {}@{} ({}).", descriptor.providerId,
                                                   descriptor.providerGeneration, descriptor.stepId.value);
            return cause.code.Value().empty() ? MakeError(ExtensionErrors::PipelineStepInvocationFailed, detail)
                                              : WrapError(ExtensionErrors::PipelineStepInvocationFailed, std::move(cause), detail);
        }

        [[nodiscard]] Error CancellationFailure(const PipelineStepDescriptor *descriptor) {
            if (descriptor == nullptr)
                return MakeError(ExtensionErrors::PipelineRunCancelled);
            return MakeError(ExtensionErrors::PipelineRunCancelled,
                             std::format("Pipeline cancelled at step: {}@{} ({}).", descriptor->providerId, descriptor->providerGeneration,
                                         descriptor->stepId.value));
        }

        [[nodiscard]] Result<std::vector<std::size_t>> ResolveOrder(
            const std::vector<std::shared_ptr<PipelineStepProviderState>> &providers) {
            std::vector<std::size_t> indegree(providers.size());
            std::vector<std::vector<std::size_t>> dependents(providers.size());
            for (std::size_t index = 0; index < providers.size(); ++index) {
                const auto &descriptor = providers[index]->descriptor;
                for (const PipelineStepId &dependency : descriptor.dependencies) {
                    const auto found = std::ranges::lower_bound(providers, dependency.value, {}, [](const auto &candidate) {
                        return candidate->descriptor.stepId.value;
                    });
                    if (found == providers.end() || (*found)->descriptor.stepId != dependency)
                        return Result<std::vector<std::size_t>>::Failure(
                            MakeError(ExtensionErrors::PipelineGraphInvalid, std::format("Pipeline step '{}' requires missing step '{}'.",
                                                                                         descriptor.stepId.value, dependency.value)));
                    const std::size_t dependencyIndex = static_cast<std::size_t>(found - providers.begin());
                    if (providers[dependencyIndex]->descriptor.phase > descriptor.phase)
                        return Result<std::vector<std::size_t>>::Failure(
                            MakeError(ExtensionErrors::PipelineGraphInvalid,
                                      std::format("Pipeline step '{}' depends on a later phase.", descriptor.stepId.value)));
                    ++indegree[index];
                    dependents[dependencyIndex].push_back(index);
                }
            }

            std::vector<std::size_t> order;
            order.reserve(providers.size());
            std::vector<bool> selected(providers.size());
            while (order.size() != providers.size()) {
                std::size_t next = providers.size();
                for (std::size_t index = 0; index < providers.size(); ++index) {
                    if (selected[index] || indegree[index] != 0U)
                        continue;
                    if (next == providers.size() ||
                        std::pair{providers[index]->descriptor.phase, providers[index]->descriptor.stepId.value} <
                            std::pair{providers[next]->descriptor.phase, providers[next]->descriptor.stepId.value})
                        next = index;
                }
                if (next == providers.size())
                    return Result<std::vector<std::size_t>>::Failure(
                        MakeError(ExtensionErrors::PipelineGraphCycle, "Pipeline step dependencies contain a cycle."));
                selected[next] = true;
                order.push_back(next);
                for (const std::size_t dependent : dependents[next])
                    --indegree[dependent];
            }
            return Result<std::vector<std::size_t>>::Success(std::move(order));
        }
    }  // namespace

    PipelineStepContext::PipelineStepContext(const std::span<const PipelineArtifactView> artifacts) noexcept : artifacts_(artifacts) {}

    /** @copydoc PipelineStepContext::Find */
    const PipelineArtifactView *PipelineStepContext::Find(const PipelineArtifactId &id) const noexcept {
        const auto found =
            std::ranges::lower_bound(artifacts_, id.value, {}, [](const PipelineArtifactView &artifact) -> const std::string & {
            return artifact.id.value;
        });
        return found != artifacts_.end() && found->id == id ? &*found : nullptr;
    }

    PipelineOutputSink::PipelineOutputSink(const std::span<const PipelineArtifactId> declaredOutputs,
                                           const std::uint64_t maximumBytes) noexcept
        : declaredOutputs_(declaredOutputs), maximumBytes_(maximumBytes) {
        outputs_.reserve(declaredOutputs.size());
    }

    /** @copydoc PipelineOutputSink::Write */
    Result<void> PipelineOutputSink::Write(const PipelineArtifactId &id, const std::span<const std::byte> bytes) {
        if (!std::ranges::binary_search(declaredOutputs_, id.value, {}, &PipelineArtifactId::value) ||
            std::ranges::any_of(outputs_,
                                [&id](const PipelineArtifact &output) {
            return output.id == id;
        }) ||
            bytes.size() > maximumBytes_ - writtenBytes_)
            return Result<void>::Failure(MakeError(ExtensionErrors::PipelineOutputInvalid));
        outputs_.push_back({id, std::vector<std::byte>(bytes.begin(), bytes.end())});
        writtenBytes_ += bytes.size();
        return Result<void>::Success();
    }

    Result<std::vector<PipelineArtifact>> PipelineOutputSink::Complete() {
        if (outputs_.size() != declaredOutputs_.size())
            return Result<std::vector<PipelineArtifact>>::Failure(MakeError(ExtensionErrors::PipelineOutputInvalid));
        std::ranges::sort(outputs_, {}, [](const PipelineArtifact &artifact) -> const std::string & {
            return artifact.id.value;
        });
        return Result<std::vector<PipelineArtifact>>::Success(std::move(outputs_));
    }

    PipelineStepRegistration::PipelineStepRegistration(std::weak_ptr<PipelineStepRegistryState> registry,
                                                       std::shared_ptr<PipelineStepProviderState> provider) noexcept
        : registry_(std::move(registry)), provider_(std::move(provider)) {}

    PipelineStepRegistration::~PipelineStepRegistration() noexcept {
        try {
            Reset();
        } catch (...) {
            if (provider_ != nullptr)
                provider_->registered.store(false, std::memory_order_release);
        }
    }

    PipelineStepRegistration::PipelineStepRegistration(PipelineStepRegistration &&other) noexcept
        : registry_(std::move(other.registry_)), provider_(std::move(other.provider_)) {}

    PipelineStepRegistration &PipelineStepRegistration::operator=(PipelineStepRegistration &&other) {
        if (this == &other)
            return *this;
        Reset();
        registry_ = std::move(other.registry_);
        provider_ = std::move(other.provider_);
        return *this;
    }

    /** @copydoc PipelineStepRegistration::Reset */
    void PipelineStepRegistration::Reset() {
        if (provider_ == nullptr)
            return;
        if (auto registry = registry_.lock())
            RemoveProvider(registry, provider_);
        else
            provider_->registered.store(false, std::memory_order_release);
        provider_.reset();
        registry_.reset();
    }

    /** @copydoc PipelineStepRegistration::IsRegistered */
    bool PipelineStepRegistration::IsRegistered() const noexcept {
        return provider_ != nullptr && provider_->registered.load(std::memory_order_acquire);
    }

    PipelineStepRegistry::PipelineStepRegistry() : state_(std::make_shared<PipelineStepRegistryState>()) {
        state_->providers.reserve(MaximumProviders);
    }

    PipelineStepRegistry::~PipelineStepRegistry() noexcept {
        try {
            BeginShutdown();
        } catch (...) {
            state_.reset();
        }
    }

    PipelineStepRegistry &PipelineStepRegistry::operator=(PipelineStepRegistry &&other) {
        if (this == &other)
            return *this;
        BeginShutdown();
        state_ = std::move(other.state_);
        return *this;
    }

    /** @copydoc PipelineStepRegistry::Register */
    Result<PipelineStepRegistration> PipelineStepRegistry::Register(  // NOSONAR(cpp:S5817) Publication mutates owner lifecycle state.
        PipelineStepDescriptor descriptor, std::shared_ptr<const IPipelineStep> provider) {
        if (!ValidDescriptor(descriptor) || provider == nullptr)
            return Result<PipelineStepRegistration>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryInvalid));
        if (state_ == nullptr)
            return Result<PipelineStepRegistration>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryShutdown));

        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<PipelineStepRegistration>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryShutdown));
        const auto insertion = std::ranges::lower_bound(state_->providers, descriptor.stepId.value, {}, [](const auto &candidate) {
            return candidate->descriptor.stepId.value;
        });
        if (insertion != state_->providers.end() && (*insertion)->descriptor.stepId == descriptor.stepId)
            return Result<PipelineStepRegistration>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryDuplicate));
        if (state_->providers.size() >= MaximumProviders)
            return Result<PipelineStepRegistration>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryCapacityExceeded));
        for (const auto &existing : state_->providers) {
            for (const PipelineArtifactId &output : descriptor.outputs) {
                if (std::ranges::binary_search(existing->descriptor.outputs, output.value, {}, &PipelineArtifactId::value))
                    return Result<PipelineStepRegistration>::Failure(
                        MakeError(ExtensionErrors::PipelineStepRegistryDuplicate,
                                  std::format("Pipeline output '{}' already has a producer.", output.value)));
            }
        }

        auto published = std::make_shared<PipelineStepProviderState>();
        published->descriptor = std::move(descriptor);
        published->provider = std::move(provider);
        state_->providers.insert(insertion, published);
        return Result<PipelineStepRegistration>::Success(PipelineStepRegistration{state_, std::move(published)});
    }

    /** @copydoc PipelineStepRegistry::Execute */
    Result<PipelineRunResult> PipelineStepRegistry::Execute(const std::span<const PipelineArtifactView> initialArtifacts,
                                                            const CancellationToken &cancellation) const {
        if (const auto valid = ValidateInitialArtifacts(initialArtifacts); valid.HasError())
            return Result<PipelineRunResult>::Failure(valid.ErrorValue());
        const auto state = state_;
        if (state == nullptr)
            return Result<PipelineRunResult>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryShutdown));
        auto providers = SnapshotProviders(state);
        if (!providers)
            return Result<PipelineRunResult>::Failure(MakeError(ExtensionErrors::PipelineStepRegistryShutdown));
        auto order = ResolveOrder(*providers);
        if (order.HasError())
            return Result<PipelineRunResult>::Failure(order.ErrorValue());
        if (cancellation.IsCancellationRequested())
            return Result<PipelineRunResult>::Failure(CancellationFailure(nullptr));
        for (const auto &provider : *providers) {
            for (const PipelineArtifactId &output : provider->descriptor.outputs) {
                if (std::ranges::binary_search(initialArtifacts, output.value, {}, [](const PipelineArtifactView &artifact) {
                    return artifact.id.value;
                }))
                    return Result<PipelineRunResult>::Failure(
                        MakeError(ExtensionErrors::PipelineGraphInvalid,
                                  std::format("Pipeline output '{}' conflicts with an initial artifact.", output.value)));
            }
        }

        std::vector<PipelineArtifactView> available(initialArtifacts.begin(), initialArtifacts.end());
        std::vector<PipelineArtifact> generated;
        std::size_t outputCount = 0U;
        for (const auto &provider : *providers)
            outputCount += provider->descriptor.outputs.size();
        generated.reserve(outputCount);
        PipelineRunResult result;
        result.executionOrder.reserve(providers->size());
        std::uint64_t remainingBytes = MaximumArtifactBytes;
        for (const PipelineArtifactView &artifact : initialArtifacts)
            remainingBytes -= artifact.bytes.size();

        for (const std::size_t index : order.Value()) {
            const auto &provider = (*providers)[index];
            if (cancellation.IsCancellationRequested())
                return Result<PipelineRunResult>::Failure(CancellationFailure(&provider->descriptor));

            std::vector<PipelineArtifactView> inputs;
            inputs.reserve(provider->descriptor.inputs.size());
            for (const PipelineArtifactId &required : provider->descriptor.inputs) {
                const auto found = std::ranges::lower_bound(available, required.value, {}, [](const PipelineArtifactView &artifact) {
                    return artifact.id.value;
                });
                if (found == available.end() || found->id != required)
                    return Result<PipelineRunResult>::Failure(
                        MakeError(ExtensionErrors::PipelineGraphInvalid, std::format("Pipeline step '{}' requires missing artifact '{}'.",
                                                                                     provider->descriptor.stepId.value, required.value)));
                inputs.push_back(*found);
            }

            PipelineStepContext context{inputs};
            PipelineOutputSink outputs{provider->descriptor.outputs, remainingBytes};
            try {
                auto executed = provider->provider->Execute(context, outputs, cancellation);
                if (executed.HasError())
                    return Result<PipelineRunResult>::Failure(InvocationFailure(provider->descriptor, executed.ErrorValue()));
            } catch (...) {  // NOSONAR(cpp:S1181) Trusted extension callback exception boundary.
                return Result<PipelineRunResult>::Failure(InvocationFailure(provider->descriptor));
            }
            if (cancellation.IsCancellationRequested())
                return Result<PipelineRunResult>::Failure(CancellationFailure(&provider->descriptor));
            auto completed = outputs.Complete();
            if (completed.HasError())
                return Result<PipelineRunResult>::Failure(InvocationFailure(provider->descriptor, completed.ErrorValue()));
            result.executionOrder.push_back(provider->descriptor.stepId);
            for (PipelineArtifact &artifact : completed.Value()) {
                remainingBytes -= artifact.bytes.size();
                generated.push_back(std::move(artifact));
                const PipelineArtifact &owned = generated.back();
                const auto insertion = std::ranges::lower_bound(available, owned.id.value, {}, [](const PipelineArtifactView &candidate) {
                    return candidate.id.value;
                });
                available.insert(insertion, PipelineArtifactView{owned.id, owned.bytes});
            }
        }
        if (cancellation.IsCancellationRequested())
            return Result<PipelineRunResult>::Failure(CancellationFailure(nullptr));
        std::ranges::sort(generated, {}, [](const PipelineArtifact &artifact) -> const std::string & {
            return artifact.id.value;
        });
        result.outputs = std::move(generated);
        return Result<PipelineRunResult>::Success(std::move(result));
    }

    /** @copydoc PipelineStepRegistry::BeginShutdown */
    void PipelineStepRegistry::BeginShutdown() {  // NOSONAR(cpp:S5817) Terminal admission mutation belongs to the owner facade.
        if (state_ == nullptr)
            return;
        std::scoped_lock lock{state_->mutex};
        state_->shutdown = true;
        for (const auto &provider : state_->providers)
            provider->registered.store(false, std::memory_order_release);
        state_->providers.clear();
    }

    /** @copydoc PipelineStepRegistry::IsShutdown */
    bool PipelineStepRegistry::IsShutdown() const {
        if (state_ == nullptr)
            return true;
        std::scoped_lock lock{state_->mutex};
        return state_->shutdown;
    }
}  // namespace Horo::Extensions
