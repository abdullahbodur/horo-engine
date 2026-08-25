#include "Horo/Foundation/ModuleDescriptor.h"

#include "foundation/FoundationErrors.h"

#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace Horo {
    namespace {
        [[nodiscard]] bool IsCanonicalId(const std::string_view value) {
            if (value.empty())
                return false;

            bool previousWasSeparator = true;
            for (const unsigned char character : value) {
                const bool separator = character == '.' || character == '-' || character == '_';
                if (separator) {
                    if (previousWasSeparator)
                        return false;
                    previousWasSeparator = true;
                    continue;
                }

                const bool lowercaseAscii = character >= 'a' && character <= 'z';
                const bool digitAscii = character >= '0' && character <= '9';
                if (!lowercaseAscii && !digitAscii)
                    return false;
                previousWasSeparator = false;
            }
            return !previousWasSeparator;
        }

        [[nodiscard]] Result<ValidatedModuleGraph> Fail(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<ValidatedModuleGraph>::Failure(MakeError(descriptor, std::move(message)));
        }

        [[nodiscard]] bool HasPairedCallbacks(const ModuleLifecycleCallbacks &callbacks) noexcept {
            return (callbacks.activate == nullptr) == (callbacks.deactivate == nullptr);
        }

        [[nodiscard]] bool IsNamespacedBy(const std::string_view child, const std::string_view parent) {
            return child.size() > parent.size() && child.starts_with(parent) && child[parent.size()] == '.';
        }

        [[nodiscard]] bool ValidateLocalDescriptor(const ModuleDescriptor &descriptor, std::string &message) {
            if (!IsCanonicalId(descriptor.id.value)) {
                message = "Module identity is not canonical: '" + descriptor.id.value + "'.";
                return false;
            }
            if (!HasPairedCallbacks(descriptor.lifecycle)) {
                message = "Module '" + descriptor.id.value + "' must declare both lifecycle callbacks or neither.";
                return false;
            }

            std::set<std::string, std::less<>> dependencies;
            for (const ModuleDependency &dependency : descriptor.dependencies) {
                if (!IsCanonicalId(dependency.module.value)) {
                    message = "Module '" + descriptor.id.value + "' has a non-canonical dependency identity.";
                    return false;
                }
                if (dependency.module == descriptor.id) {
                    message = "Module '" + descriptor.id.value + "' depends on itself.";
                    return false;
                }
                if (!dependencies.emplace(dependency.module.value).second) {
                    message = "Module '" + descriptor.id.value + "' repeats dependency '" + dependency.module.value + "'.";
                    return false;
                }
            }

            std::set<std::string, std::less<>> providedCapabilities;
            for (const ModuleCapabilityId &capability : descriptor.providedCapabilities) {
                if (!IsCanonicalId(capability.value) || !providedCapabilities.emplace(capability.value).second) {
                    message = "Module '" + descriptor.id.value + "' has an invalid or duplicate provided capability.";
                    return false;
                }
            }

            std::set<std::string, std::less<>> requiredCapabilities;
            for (const ModuleCapabilityId &capability : descriptor.requiredCapabilities) {
                if (!IsCanonicalId(capability.value) || !requiredCapabilities.emplace(capability.value).second) {
                    message = "Module '" + descriptor.id.value + "' has an invalid or duplicate required capability.";
                    return false;
                }
                if (providedCapabilities.contains(capability.value)) {
                    message = "Module '" + descriptor.id.value + "' cannot require a capability it provides.";
                    return false;
                }
            }

            std::set<std::string, std::less<>> budgetIds;
            for (const ModuleResourceBudget &budget : descriptor.resourceBudgets) {
                if (!IsCanonicalId(budget.id) || !IsNamespacedBy(budget.id, descriptor.id.value) || budget.limit == 0 ||
                    !budgetIds.emplace(budget.id).second) {
                    message = "Module '" + descriptor.id.value + "' has a zero or duplicate resource budget.";
                    return false;
                }
            }

            std::set<std::pair<ModuleObservabilityKind, std::string>> observability;
            for (const ModuleObservabilityDescriptor &entry : descriptor.observability) {
                if (!IsCanonicalId(entry.id) || !IsNamespacedBy(entry.id, descriptor.id.value) ||
                    !observability.emplace(entry.kind, entry.id).second) {
                    message = "Module '" + descriptor.id.value + "' has an invalid or duplicate observability descriptor.";
                    return false;
                }
            }
            return true;
        }
    }  // namespace

    /** @copydoc ValidateModuleGraph */
    Result<ValidatedModuleGraph> ValidateModuleGraph(const std::span<const ModuleDescriptor> descriptors) {
        std::map<std::string, std::size_t, std::less<>> modules;
        for (std::size_t index = 0; index < descriptors.size(); ++index) {
            std::string message;
            if (!ValidateLocalDescriptor(descriptors[index], message))
                return Fail(ModuleDescriptorErrors::InvalidDescriptor, std::move(message));
            if (!modules.emplace(descriptors[index].id.value, index).second) {
                return Fail(ModuleDescriptorErrors::DuplicateModule, "Duplicate module identity: '" + descriptors[index].id.value + "'.");
            }
        }

        std::map<std::string, std::vector<std::size_t>, std::less<>> capabilityProviders;
        for (std::size_t index = 0; index < descriptors.size(); ++index) {
            for (const ModuleCapabilityId &capability : descriptors[index].providedCapabilities)
                capabilityProviders[capability.value].push_back(index);
        }

        std::vector<std::set<std::size_t>> outgoing(descriptors.size());
        std::vector<std::size_t> incomingCount(descriptors.size());
        const auto addEdge = [&outgoing, &incomingCount](const std::size_t provider, const std::size_t dependant) {
            if (outgoing[provider].emplace(dependant).second)
                ++incomingCount[dependant];
        };

        for (std::size_t dependant = 0; dependant < descriptors.size(); ++dependant) {
            const ModuleDescriptor &descriptor = descriptors[dependant];
            for (const ModuleDependency &dependency : descriptor.dependencies) {
                const auto provider = modules.find(dependency.module.value);
                if (provider == modules.end()) {
                    if (dependency.kind == ModuleDependencyKind::Required) {
                        return Fail(ModuleDescriptorErrors::MissingDependency,
                                    "Module '" + descriptor.id.value + "' requires missing module '" + dependency.module.value + "'.");
                    }
                    continue;
                }
                if (descriptors[provider->second].version < dependency.minimumVersion) {
                    return Fail(ModuleDescriptorErrors::IncompatibleDependency,
                                "Module '" + descriptor.id.value + "' requires a newer contract from '" + dependency.module.value + "'.");
                }
                addEdge(provider->second, dependant);
            }

            for (const ModuleCapabilityId &capability : descriptor.requiredCapabilities) {
                const auto providers = capabilityProviders.find(capability.value);
                if (providers == capabilityProviders.end()) {
                    return Fail(ModuleDescriptorErrors::MissingCapability,
                                "Module '" + descriptor.id.value + "' requires missing capability '" + capability.value + "'.");
                }
                for (const std::size_t provider : providers->second)
                    addEdge(provider, dependant);
            }
        }

        const auto compareById = [&descriptors](const std::size_t left, const std::size_t right) {
            return descriptors[left].id.value < descriptors[right].id.value;
        };
        std::set<std::size_t, decltype(compareById)> ready(compareById);
        for (std::size_t index = 0; index < incomingCount.size(); ++index) {
            if (incomingCount[index] == 0)
                ready.emplace(index);
        }

        ValidatedModuleGraph graph;
        graph.initializationOrder.reserve(descriptors.size());
        while (!ready.empty()) {
            const std::size_t provider = *ready.begin();
            ready.erase(ready.begin());
            graph.initializationOrder.push_back(descriptors[provider].id);
            for (const std::size_t dependant : outgoing[provider]) {
                if (--incomingCount[dependant] == 0)
                    ready.emplace(dependant);
            }
        }

        if (graph.initializationOrder.size() != descriptors.size())
            return Fail(ModuleDescriptorErrors::DependencyCycle, "Module descriptor graph contains a dependency cycle.");
        return Result<ValidatedModuleGraph>::Success(std::move(graph));
    }
}  // namespace Horo
