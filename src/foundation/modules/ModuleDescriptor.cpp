#include "Horo/Foundation/ModuleDescriptor.h"

#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace Horo {
    namespace {
        /** @brief Returns whether a canonical identifier byte separates segments. */
        [[nodiscard]] bool IsIdSeparator(const unsigned char character) noexcept {
            return character == '.' || character == '-' || character == '_';
        }

        /** @brief Returns whether a byte is valid inside a canonical identifier segment. */
        [[nodiscard]] bool IsLowercaseAsciiAlphanumeric(const unsigned char character) noexcept {
            const bool lowercaseAscii = character >= 'a' && character <= 'z';
            const bool digitAscii = character >= '0' && character <= '9';
            return lowercaseAscii || digitAscii;
        }

        /** @brief Validates a single character and updates separator tracking in a canonical identifier. */
        [[nodiscard]] bool IsValidSegmentChar(const unsigned char character, bool &previousWasSeparator) noexcept {
            if (IsIdSeparator(character)) {
                if (previousWasSeparator)
                    return false;
                previousWasSeparator = true;
                return true;
            }
            if (!IsLowercaseAsciiAlphanumeric(character))
                return false;
            previousWasSeparator = false;
            return true;
        }

        /** @brief Validates the lowercase ASCII segmented identifier grammar. */
        [[nodiscard]] bool IsCanonicalId(const std::string_view value) {
            if (value.empty())
                return false;

            bool previousWasSeparator = true;
            for (const unsigned char character : value) {
                if (!IsValidSegmentChar(character, previousWasSeparator))
                    return false;
            }
            return !previousWasSeparator;
        }

        /** @brief Creates a typed validation failure from a stable error descriptor. */
        template <typename ValueT> [[nodiscard]] Result<ValueT> Fail(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<ValueT>::Failure(MakeError(descriptor, std::move(message)));
        }

        /** @brief Returns whether lifecycle callbacks form one valid activation lifetime. */
        [[nodiscard]] bool HasValidLifecycleCallbacks(const ModuleLifecycleCallbacks &callbacks) noexcept {
            const bool hasPairedEndpoints = (callbacks.activate == nullptr) == (callbacks.deactivate == nullptr);
            const bool drainHasLifetime = callbacks.drain == nullptr || callbacks.activate != nullptr;
            return hasPairedEndpoints && drainHasLifetime;
        }

        /** @brief Returns whether a child identity is strictly nested below its parent identity. */
        [[nodiscard]] bool IsNamespacedBy(const std::string_view child, const std::string_view parent) {
            return child.size() > parent.size() && child.starts_with(parent) && child[parent.size()] == '.';
        }

        /** @brief Validates one descriptor's dependency identities and uniqueness. */
        [[nodiscard]] std::optional<std::string> ValidateDependencies(const ModuleDescriptor &descriptor) {
            std::set<std::string, std::less<>> dependencies;
            for (const ModuleDependency &dependency : descriptor.dependencies) {
                if (!IsCanonicalId(dependency.module.value))
                    return "Module '" + descriptor.id.value + "' has a non-canonical dependency identity.";
                if (dependency.module == descriptor.id)
                    return "Module '" + descriptor.id.value + "' depends on itself.";
                if (!dependencies.emplace(dependency.module.value).second)
                    return "Module '" + descriptor.id.value + "' repeats dependency '" + dependency.module.value + "'.";
            }
            return std::nullopt;
        }

        /** @brief Validates one descriptor's provided capability identities and uniqueness. */
        [[nodiscard]] std::optional<std::string> ValidateProvidedCapabilities(const ModuleDescriptor &descriptor) {
            std::set<std::string, std::less<>> providedCapabilities;
            for (const ModuleCapabilityId &capability : descriptor.providedCapabilities) {
                if (!IsCanonicalId(capability.value))
                    return "Module '" + descriptor.id.value + "' has a non-canonical provided capability.";
                if (!providedCapabilities.emplace(capability.value).second)
                    return "Module '" + descriptor.id.value + "' repeats provided capability '" + capability.value + "'.";
            }
            return std::nullopt;
        }

        /** @brief Validates one descriptor's required capabilities and self-provisioning rule. */
        [[nodiscard]] std::optional<std::string> ValidateRequiredCapabilities(const ModuleDescriptor &descriptor) {
            std::set<std::string, std::less<>> requiredCapabilities;
            for (const ModuleCapabilityId &capability : descriptor.requiredCapabilities) {
                if (!IsCanonicalId(capability.value))
                    return "Module '" + descriptor.id.value + "' has a non-canonical required capability.";
                if (!requiredCapabilities.emplace(capability.value).second)
                    return "Module '" + descriptor.id.value + "' repeats required capability '" + capability.value + "'.";
                if (std::ranges::find(descriptor.providedCapabilities, capability) != descriptor.providedCapabilities.end())
                    return "Module '" + descriptor.id.value + "' cannot require a capability it provides.";
            }
            return std::nullopt;
        }

        /** @brief Validates one resource budget entry within a descriptor. */
        [[nodiscard]] std::optional<std::string> ValidateBudgetEntry(const ModuleResourceBudget &budget, const std::string_view moduleId,
                                                                     std::set<std::string, std::less<>> &budgetIds) {
            if (!IsCanonicalId(budget.id))
                return "Module '" + std::string(moduleId) + "' has a non-canonical resource budget identity.";
            if (!IsNamespacedBy(budget.id, moduleId))
                return "Resource budget '" + budget.id + "' is not namespaced by module '" + std::string(moduleId) + "'.";
            if (budget.limit == 0)
                return "Resource budget '" + budget.id + "' has a zero limit.";
            if (!budgetIds.emplace(budget.id).second)
                return "Module '" + std::string(moduleId) + "' repeats resource budget '" + budget.id + "'.";
            return std::nullopt;
        }

        /** @brief Validates one descriptor's named resource budget hints. */
        [[nodiscard]] std::optional<std::string> ValidateBudgets(const ModuleDescriptor &descriptor) {
            std::set<std::string, std::less<>> budgetIds;
            for (const ModuleResourceBudget &budget : descriptor.resourceBudgets) {
                if (auto failure = ValidateBudgetEntry(budget, descriptor.id.value, budgetIds); failure.has_value())
                    return failure;
            }
            return std::nullopt;
        }

        /** @brief Validates one observability entry within a descriptor. */
        [[nodiscard]] std::optional<std::string> ValidateObservabilityEntry(
            const ModuleObservabilityDescriptor &entry, const std::string_view moduleId,
            std::set<std::pair<ModuleObservabilityKind, std::string>> &observability) {
            if (!IsCanonicalId(entry.id))
                return "Module '" + std::string(moduleId) + "' has a non-canonical observability identity.";
            if (!IsNamespacedBy(entry.id, moduleId))
                return "Observability descriptor '" + entry.id + "' is not namespaced by module '" + std::string(moduleId) + "'.";
            if (!observability.emplace(entry.kind, entry.id).second)
                return "Module '" + std::string(moduleId) + "' repeats observability descriptor '" + entry.id + "'.";
            return std::nullopt;
        }

        /** @brief Validates one descriptor's module-namespaced observability entries. */
        [[nodiscard]] std::optional<std::string> ValidateObservability(const ModuleDescriptor &descriptor) {
            std::set<std::pair<ModuleObservabilityKind, std::string>> observability;
            for (const ModuleObservabilityDescriptor &entry : descriptor.observability) {
                if (auto failure = ValidateObservabilityEntry(entry, descriptor.id.value, observability); failure.has_value())
                    return failure;
            }
            return std::nullopt;
        }

        /** @brief Runs every descriptor-local validation without consulting graph state. */
        [[nodiscard]] std::optional<std::string> ValidateLocalDescriptor(const ModuleDescriptor &descriptor) {
            if (!IsCanonicalId(descriptor.id.value))
                return "Module identity is not canonical: '" + descriptor.id.value + "'.";
            if (!HasValidLifecycleCallbacks(descriptor.lifecycle)) {
                return "Module '" + descriptor.id.value +
                       "' must pair activation and deactivation, and may drain only within that lifetime.";
            }
            using ValidatorFn = std::optional<std::string> (*)(const ModuleDescriptor &);
            constexpr std::array<ValidatorFn, 5> kValidators = {
                &ValidateDependencies, &ValidateProvidedCapabilities, &ValidateRequiredCapabilities,
                &ValidateBudgets,      &ValidateObservability,
            };
            for (const auto validator : kValidators) {
                if (auto failure = validator(descriptor); failure.has_value())
                    return failure;
            }
            return std::nullopt;
        }

        using ModuleIndex = std::map<std::string, std::size_t, std::less<>>;
        using CapabilityProviderIndex = std::map<std::string, std::vector<std::size_t>, std::less<>>;

        /** @brief Mutable edge storage used only while validating and ordering a graph. */
        struct GraphEdges {
            std::vector<std::set<std::size_t>> outgoing;
            std::vector<std::size_t> incomingCount;
        };

        /** @brief Validates descriptors and indexes each unique module identity. */
        [[nodiscard]] Result<ModuleIndex> IndexModules(const std::span<const ModuleDescriptor> descriptors) {
            ModuleIndex modules;
            for (std::size_t index = 0; index < descriptors.size(); ++index) {
                if (auto failure = ValidateLocalDescriptor(descriptors[index]); failure.has_value())
                    return Fail<ModuleIndex>(ModuleDescriptorErrors::InvalidDescriptor, std::move(*failure));
                if (!modules.try_emplace(descriptors[index].id.value, index).second) {
                    return Fail<ModuleIndex>(ModuleDescriptorErrors::DuplicateModule,
                                             "Duplicate module identity: '" + descriptors[index].id.value + "'.");
                }
            }
            return Result<ModuleIndex>::Success(std::move(modules));
        }

        /** @brief Indexes every selected provider by capability identity. */
        [[nodiscard]] CapabilityProviderIndex IndexCapabilityProviders(const std::span<const ModuleDescriptor> descriptors) {
            CapabilityProviderIndex providers;
            for (std::size_t index = 0; index < descriptors.size(); ++index) {
                for (const ModuleCapabilityId &capability : descriptors[index].providedCapabilities)
                    providers[capability.value].push_back(index);
            }
            return providers;
        }

        /** @brief Adds one unique provider-to-dependant edge and updates its indegree. */
        void AddEdge(GraphEdges &edges, const std::size_t provider, const std::size_t dependant) {
            if (edges.outgoing[provider].emplace(dependant).second)
                ++edges.incomingCount[dependant];
        }

        /** @brief Resolves and validates explicit module dependencies for one dependant. */
        [[nodiscard]] Result<void> AddDependencyEdges(const std::span<const ModuleDescriptor> descriptors, const ModuleIndex &modules,
                                                      const std::size_t dependant, GraphEdges &edges) {
            const ModuleDescriptor &descriptor = descriptors[dependant];
            for (const ModuleDependency &dependency : descriptor.dependencies) {
                const auto provider = modules.find(dependency.module.value);
                if (provider == modules.end()) {
                    if (dependency.kind == ModuleDependencyKind::Required) {
                        return Fail<void>(ModuleDescriptorErrors::MissingDependency, "Module '" + descriptor.id.value +
                                                                                         "' requires missing module '" +
                                                                                         dependency.module.value + "'.");
                    }
                    continue;
                }
                if (descriptors[provider->second].version < dependency.minimumVersion) {
                    return Fail<void>(ModuleDescriptorErrors::IncompatibleDependency, "Module '" + descriptor.id.value +
                                                                                          "' requires a newer contract from '" +
                                                                                          dependency.module.value + "'.");
                }
                AddEdge(edges, provider->second, dependant);
            }
            return Result<void>::Success();
        }

        /** @brief Resolves capability providers and adds their ordering edges for one dependant. */
        [[nodiscard]] Result<void> AddCapabilityEdges(const std::span<const ModuleDescriptor> descriptors,
                                                      const CapabilityProviderIndex &capabilityProviders, const std::size_t dependant,
                                                      GraphEdges &edges) {
            const ModuleDescriptor &descriptor = descriptors[dependant];
            for (const ModuleCapabilityId &capability : descriptor.requiredCapabilities) {
                const auto providers = capabilityProviders.find(capability.value);
                if (providers == capabilityProviders.end()) {
                    return Fail<void>(ModuleDescriptorErrors::MissingCapability,
                                      "Module '" + descriptor.id.value + "' requires missing capability '" + capability.value + "'.");
                }
                for (const std::size_t provider : providers->second)
                    AddEdge(edges, provider, dependant);
            }
            return Result<void>::Success();
        }

        /** @brief Builds validated dependency and capability edges for every selected module. */
        [[nodiscard]] Result<GraphEdges> BuildEdges(const std::span<const ModuleDescriptor> descriptors, const ModuleIndex &modules,
                                                    const CapabilityProviderIndex &capabilityProviders) {
            GraphEdges edges{.outgoing = std::vector<std::set<std::size_t>>(descriptors.size()),
                             .incomingCount = std::vector<std::size_t>(descriptors.size())};
            for (std::size_t dependant = 0; dependant < descriptors.size(); ++dependant) {
                if (const Result<void> dependencyResult = AddDependencyEdges(descriptors, modules, dependant, edges);
                    dependencyResult.HasError())
                    return Result<GraphEdges>::Failure(dependencyResult.ErrorValue());
                if (const Result<void> capabilityResult = AddCapabilityEdges(descriptors, capabilityProviders, dependant, edges);
                    capabilityResult.HasError())
                    return Result<GraphEdges>::Failure(capabilityResult.ErrorValue());
            }
            return Result<GraphEdges>::Success(std::move(edges));
        }

        /** @brief Produces stable provider-first order or reports a remaining cycle. */
        [[nodiscard]] Result<ValidatedModuleGraph> OrderGraph(const std::span<const ModuleDescriptor> descriptors, GraphEdges edges) {
            const auto compareById = [&descriptors](const std::size_t left, const std::size_t right) {
                return descriptors[left].id.value < descriptors[right].id.value;
            };
            std::set<std::size_t, decltype(compareById)> ready(compareById);
            for (std::size_t index = 0; index < edges.incomingCount.size(); ++index) {
                if (edges.incomingCount[index] == 0)
                    ready.emplace(index);
            }

            ValidatedModuleGraph graph;
            graph.initializationOrder.reserve(descriptors.size());
            while (!ready.empty()) {
                const std::size_t provider = *ready.begin();
                ready.erase(ready.begin());
                graph.initializationOrder.push_back(descriptors[provider].id);
                for (const std::size_t dependant : edges.outgoing[provider]) {
                    if (--edges.incomingCount[dependant] == 0)
                        ready.emplace(dependant);
                }
            }

            if (graph.initializationOrder.size() != descriptors.size()) {
                return Fail<ValidatedModuleGraph>(ModuleDescriptorErrors::DependencyCycle,
                                                  "Module descriptor graph contains a dependency cycle.");
            }
            return Result<ValidatedModuleGraph>::Success(std::move(graph));
        }
    }  // namespace

    /** @copydoc ValidateModuleGraph */
    Result<ValidatedModuleGraph> ValidateModuleGraph(const std::span<const ModuleDescriptor> descriptors) {
        Result<ModuleIndex> modules = IndexModules(descriptors);
        if (modules.HasError())
            return Result<ValidatedModuleGraph>::Failure(modules.ErrorValue());

        const CapabilityProviderIndex capabilityProviders = IndexCapabilityProviders(descriptors);
        Result<GraphEdges> edges = BuildEdges(descriptors, modules.Value(), capabilityProviders);
        if (edges.HasError())
            return Result<ValidatedModuleGraph>::Failure(edges.ErrorValue());
        return OrderGraph(descriptors, std::move(edges).Value());
    }
}  // namespace Horo
