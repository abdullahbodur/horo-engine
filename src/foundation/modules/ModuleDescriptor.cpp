#include "Horo/Foundation/ModuleDescriptor.h"

#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace Horo {
    namespace {
        // ─── Identifier helpers ───────────────────────────────────────────────

        [[nodiscard]] bool IsIdSeparator(const unsigned char ch) noexcept {
            return ch == '.' || ch == '-' || ch == '_';
        }

        [[nodiscard]] bool IsLowercaseAlphanumeric(const unsigned char ch) noexcept {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        }

        /** @brief Validates one character and updates consecutive-separator state. */
        [[nodiscard]] bool IsValidSegmentChar(const unsigned char ch, bool &prevWasSeparator) noexcept {
            if (IsIdSeparator(ch)) {
                if (prevWasSeparator)
                    return false;
                prevWasSeparator = true;
                return true;
            }
            if (!IsLowercaseAlphanumeric(ch))
                return false;
            prevWasSeparator = false;
            return true;
        }

        /** @brief Returns whether @p value conforms to the lowercase-segmented identifier grammar. */
        [[nodiscard]] bool IsCanonicalId(const std::string_view value) noexcept {
            if (value.empty())
                return false;
            bool prevWasSeparator = true;
            for (const unsigned char ch : value) {
                if (!IsValidSegmentChar(ch, prevWasSeparator))
                    return false;
            }
            return !prevWasSeparator;
        }

        /** @brief Returns whether @p child is strictly nested one level below @p parent. */
        [[nodiscard]] bool IsNamespacedBy(const std::string_view child, const std::string_view parent) noexcept {
            return child.size() > parent.size() && child.starts_with(parent) && child[parent.size()] == '.';
        }

        // ─── Result helpers ───────────────────────────────────────────────────

        /** @brief Returns a typed failure from a stable error descriptor and a formatted message. */
        template <typename T> [[nodiscard]] Result<T> Fail(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<T>::Failure(MakeError(descriptor, std::move(message)));
        }

        // ─── Per-descriptor validators ────────────────────────────────────────

        /** @brief Returns whether lifecycle callbacks form a valid paired activation lifetime. */
        [[nodiscard]] bool HasValidLifecycleCallbacks(const ModuleLifecycleCallbacks &callbacks) noexcept {
            const bool paired = (callbacks.activate == nullptr) == (callbacks.deactivate == nullptr);
            const bool drainWithinLifetime = callbacks.drain == nullptr || callbacks.activate != nullptr;
            return paired && drainWithinLifetime;
        }

        [[nodiscard]] std::optional<std::string> ValidateDependencies(const ModuleDescriptor &d) {
            std::set<std::string, std::less<>> seen;
            for (const ModuleDependency &dep : d.dependencies) {
                if (!IsCanonicalId(dep.module.value))
                    return std::format("Module '{}' has a non-canonical dependency identity.", d.id.value);
                if (dep.module == d.id)
                    return std::format("Module '{}' depends on itself.", d.id.value);
                if (!seen.emplace(dep.module.value).second)
                    return std::format("Module '{}' repeats dependency '{}'.", d.id.value, dep.module.value);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> ValidateProvidedCapabilities(const ModuleDescriptor &d) {
            std::set<std::string, std::less<>> seen;
            for (const ModuleCapabilityId &cap : d.providedCapabilities) {
                if (!IsCanonicalId(cap.value))
                    return std::format("Module '{}' has a non-canonical provided capability.", d.id.value);
                if (!seen.emplace(cap.value).second)
                    return std::format("Module '{}' repeats provided capability '{}'.", d.id.value, cap.value);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> ValidateRequiredCapabilities(const ModuleDescriptor &d) {
            std::set<std::string, std::less<>> seen;
            for (const ModuleCapabilityId &cap : d.requiredCapabilities) {
                if (!IsCanonicalId(cap.value))
                    return std::format("Module '{}' has a non-canonical required capability.", d.id.value);
                if (!seen.emplace(cap.value).second)
                    return std::format("Module '{}' repeats required capability '{}'.", d.id.value, cap.value);
                if (std::ranges::find(d.providedCapabilities, cap) != d.providedCapabilities.end())
                    return std::format("Module '{}' cannot require a capability it provides.", d.id.value);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> ValidateBudgets(const ModuleDescriptor &d) {
            std::set<std::string, std::less<>> seen;
            for (const ModuleResourceBudget &budget : d.resourceBudgets) {
                if (!IsCanonicalId(budget.id))
                    return std::format("Module '{}' has a non-canonical resource budget identity.", d.id.value);
                if (!IsNamespacedBy(budget.id, d.id.value))
                    return std::format("Resource budget '{}' is not namespaced by module '{}'.", budget.id, d.id.value);
                if (budget.limit == 0)
                    return std::format("Resource budget '{}' has a zero limit.", budget.id);
                if (!seen.emplace(budget.id).second)
                    return std::format("Module '{}' repeats resource budget '{}'.", d.id.value, budget.id);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> ValidateObservability(const ModuleDescriptor &d) {
            std::set<std::pair<ModuleObservabilityKind, std::string>> seen;
            for (const ModuleObservabilityDescriptor &entry : d.observability) {
                if (!IsCanonicalId(entry.id))
                    return std::format("Module '{}' has a non-canonical observability identity.", d.id.value);
                if (!IsNamespacedBy(entry.id, d.id.value))
                    return std::format("Observability descriptor '{}' is not namespaced by module '{}'.", entry.id, d.id.value);
                if (!seen.emplace(entry.kind, entry.id).second)
                    return std::format("Module '{}' repeats observability descriptor '{}'.", d.id.value, entry.id);
            }
            return std::nullopt;
        }

        /** @brief Runs all per-descriptor validators in sequence; returns the first failure. */
        [[nodiscard]] std::optional<std::string> ValidateLocalDescriptor(const ModuleDescriptor &d) {
            if (!IsCanonicalId(d.id.value))
                return std::format("Module identity is not canonical: '{}'.", d.id.value);
            if (!HasValidLifecycleCallbacks(d.lifecycle))
                return std::format("Module '{}' must pair activation and deactivation, and may drain only within that lifetime.",
                                   d.id.value);

            using Validator = std::optional<std::string> (*)(const ModuleDescriptor &);
            constexpr std::array<Validator, 5> kValidators = {
                ValidateDependencies, ValidateProvidedCapabilities, ValidateRequiredCapabilities, ValidateBudgets, ValidateObservability,
            };
            for (const auto validate : kValidators) {
                if (auto failure = validate(d))
                    return failure;
            }
            return std::nullopt;
        }

        // ─── Graph types ──────────────────────────────────────────────────────

        using ModuleIndex = std::map<std::string, std::size_t, std::less<>>;
        using CapabilityProviderIndex = std::map<std::string, std::vector<std::size_t>, std::less<>>;

        /** @brief Mutable edge storage used only while validating and ordering a graph. */
        struct GraphEdges {
            std::vector<std::set<std::size_t>> outgoing;
            std::vector<std::size_t> incomingCount;
        };

        // ─── Graph construction ───────────────────────────────────────────────

        /** @brief Validates descriptors and indexes each unique module identity. */
        [[nodiscard]] Result<ModuleIndex> IndexModules(const std::span<const ModuleDescriptor> descriptors) {
            ModuleIndex modules;
            for (std::size_t i = 0; i < descriptors.size(); ++i) {
                if (auto failure = ValidateLocalDescriptor(descriptors[i]))
                    return Fail<ModuleIndex>(ModuleDescriptorErrors::InvalidDescriptor, std::move(*failure));
                if (!modules.try_emplace(descriptors[i].id.value, i).second)
                    return Fail<ModuleIndex>(ModuleDescriptorErrors::DuplicateModule,
                                             std::format("Duplicate module identity: '{}'.", descriptors[i].id.value));
            }
            return Result<ModuleIndex>::Success(std::move(modules));
        }

        /** @brief Indexes every provider by capability identity. */
        [[nodiscard]] CapabilityProviderIndex IndexCapabilityProviders(const std::span<const ModuleDescriptor> descriptors) {
            CapabilityProviderIndex providers;
            for (std::size_t i = 0; i < descriptors.size(); ++i) {
                for (const ModuleCapabilityId &cap : descriptors[i].providedCapabilities)
                    providers[cap.value].push_back(i);
            }
            return providers;
        }

        /** @brief Adds one unique provider-to-dependant edge and increments its indegree. */
        void AddEdge(GraphEdges &edges, const std::size_t provider, const std::size_t dependant) {
            if (edges.outgoing[provider].emplace(dependant).second)
                ++edges.incomingCount[dependant];
        }

        /** @brief Resolves one explicit dependency to its provider index, or fails with a typed error. */
        [[nodiscard]] Result<std::optional<std::size_t>> ResolveDependencyProvider(const ModuleDescriptor &d, const ModuleDependency &dep,
                                                                                   const std::span<const ModuleDescriptor> descriptors,
                                                                                   const ModuleIndex &modules) {
            const auto it = modules.find(dep.module.value);
            if (it == modules.end()) {
                if (dep.kind == ModuleDependencyKind::Required)
                    return Fail<std::optional<std::size_t>>(ModuleDescriptorErrors::MissingDependency,
                                                            std::format("Module '{}' requires missing module '{}'.", d.id.value,
                                                                        dep.module.value));
                return Result<std::optional<std::size_t>>::Success(std::nullopt);
            }
            if (descriptors[it->second].version < dep.minimumVersion)
                return Fail<std::optional<std::size_t>>(ModuleDescriptorErrors::IncompatibleDependency,
                                                        std::format("Module '{}' requires a newer contract from '{}'.", d.id.value,
                                                                    dep.module.value));
            return Result<std::optional<std::size_t>>::Success(it->second);
        }

        /** @brief Resolves explicit module dependencies for one dependant and records edges. */
        [[nodiscard]] Result<void> AddDependencyEdges(const std::span<const ModuleDescriptor> descriptors, const ModuleIndex &modules,
                                                      const std::size_t dependant, GraphEdges &edges) {
            const ModuleDescriptor &d = descriptors[dependant];
            for (const ModuleDependency &dep : d.dependencies) {
                auto provider = ResolveDependencyProvider(d, dep, descriptors, modules);
                if (provider.HasError())
                    return Result<void>::Failure(provider.ErrorValue());
                if (provider.Value())
                    AddEdge(edges, *provider.Value(), dependant);
            }
            return Result<void>::Success();
        }

        /** @brief Resolves required capabilities for one dependant and records ordering edges. */
        [[nodiscard]] Result<void> AddCapabilityEdges(const std::span<const ModuleDescriptor> descriptors,
                                                      const CapabilityProviderIndex &capabilityProviders, const std::size_t dependant,
                                                      GraphEdges &edges) {
            const ModuleDescriptor &d = descriptors[dependant];
            for (const ModuleCapabilityId &cap : d.requiredCapabilities) {
                const auto it = capabilityProviders.find(cap.value);
                if (it == capabilityProviders.end())
                    return Fail<void>(ModuleDescriptorErrors::MissingCapability,
                                      std::format("Module '{}' requires missing capability '{}'.", d.id.value, cap.value));
                for (const std::size_t provider : it->second)
                    AddEdge(edges, provider, dependant);
            }
            return Result<void>::Success();
        }

        /** @brief Builds validated dependency and capability edges for every module. */
        [[nodiscard]] Result<GraphEdges> BuildEdges(const std::span<const ModuleDescriptor> descriptors, const ModuleIndex &modules,
                                                    const CapabilityProviderIndex &capabilityProviders) {
            GraphEdges edges{.outgoing = std::vector<std::set<std::size_t>>(descriptors.size()),
                             .incomingCount = std::vector<std::size_t>(descriptors.size())};
            for (std::size_t i = 0; i < descriptors.size(); ++i) {
                if (auto r = AddDependencyEdges(descriptors, modules, i, edges); r.HasError())
                    return Result<GraphEdges>::Failure(r.ErrorValue());
                if (auto r = AddCapabilityEdges(descriptors, capabilityProviders, i, edges); r.HasError())
                    return Result<GraphEdges>::Failure(r.ErrorValue());
            }
            return Result<GraphEdges>::Success(std::move(edges));
        }

        /** @brief Produces a stable provider-first initialization order, or reports a cycle. */
        [[nodiscard]] Result<ValidatedModuleGraph> OrderGraph(const std::span<const ModuleDescriptor> descriptors, GraphEdges edges) {
            const auto compareById = [&descriptors](const std::size_t a, const std::size_t b) {
                return descriptors[a].id.value < descriptors[b].id.value;
            };
            std::set<std::size_t, decltype(compareById)> ready(compareById);
            for (std::size_t i = 0; i < edges.incomingCount.size(); ++i) {
                if (edges.incomingCount[i] == 0)
                    ready.emplace(i);
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

            if (graph.initializationOrder.size() != descriptors.size())
                return Fail<ValidatedModuleGraph>(ModuleDescriptorErrors::DependencyCycle,
                                                  "Module descriptor graph contains a dependency cycle.");
            return Result<ValidatedModuleGraph>::Success(std::move(graph));
        }
    }  // namespace

    /** @copydoc ValidateModuleGraph */
    Result<ValidatedModuleGraph> ValidateModuleGraph(const std::span<const ModuleDescriptor> descriptors) {
        auto modules = IndexModules(descriptors);
        if (modules.HasError())
            return Result<ValidatedModuleGraph>::Failure(modules.ErrorValue());

        const CapabilityProviderIndex capabilityProviders = IndexCapabilityProviders(descriptors);
        auto edges = BuildEdges(descriptors, modules.Value(), capabilityProviders);
        if (edges.HasError())
            return Result<ValidatedModuleGraph>::Failure(edges.ErrorValue());
        return OrderGraph(descriptors, std::move(edges).Value());
    }
}  // namespace Horo
