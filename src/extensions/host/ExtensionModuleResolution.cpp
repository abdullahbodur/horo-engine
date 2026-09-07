#include "Horo/Extensions/ExtensionModuleResolution.h"

#include "Horo/Extensions/ExtensionErrors.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>

namespace Horo::Extensions {
    namespace {
        struct SemanticVersionCore {
            std::uint64_t major{};
            std::uint64_t minor{};
            std::uint64_t patch{};
            bool prerelease{};
        };

        [[nodiscard]] std::optional<SemanticVersionCore> ParseVersionCore(std::string_view version) {
            SemanticVersionCore result;
            result.prerelease = version.find('-') != std::string_view::npos;
            const std::array parts{&result.major, &result.minor, &result.patch};
            for (std::uint64_t *part : parts) {
                const std::size_t end = version.find_first_of(".-");
                const std::string_view digits = version.substr(0, end);
                const auto [parsedEnd, error] = std::from_chars(digits.data(), digits.data() + digits.size(), *part);
                if (error != std::errc{} || parsedEnd != digits.data() + digits.size())
                    return std::nullopt;
                if (end == std::string_view::npos)
                    break;
                version.remove_prefix(end + 1);
            }
            return result;
        }

        [[nodiscard]] bool IsCompatibleVersion(const std::string_view provided, const std::string_view required) {
            const auto actual = ParseVersionCore(provided);
            const auto minimum = ParseVersionCore(required);
            if (!actual.has_value() || !minimum.has_value() || actual->major != minimum->major)
                return false;
            const auto actualCore = std::tie(actual->minor, actual->patch);
            const auto minimumCore = std::tie(minimum->minor, minimum->patch);
            return actualCore > minimumCore || (actualCore == minimumCore && (!actual->prerelease || minimum->prerelease));
        }

        [[nodiscard]] bool IsPresentationOnly(const ExtensionModuleManifest &moduleManifest) {
            return moduleManifest.roles.size() == 1 && moduleManifest.roles.front() == ExtensionModuleRole::EditorPresentation;
        }

        [[nodiscard]] std::string JoinModuleIds(const std::set<std::string, std::less<>> &moduleIds) {
            std::ostringstream output;
            for (auto iterator = moduleIds.begin(); iterator != moduleIds.end(); ++iterator) {
                if (iterator != moduleIds.begin())
                    output << ", ";
                output << *iterator;
            }
            return std::move(output).str();
        }

        [[nodiscard]] Result<ExtensionModulePlan> Failure(const std::string_view reason,
                                                          const std::set<std::string, std::less<>> &moduleIds) {
            return Result<ExtensionModulePlan>::Failure(
                MakeError(ExtensionErrors::ModuleResolutionFailed,
                          std::string{reason} + " Involved modules: " + JoinModuleIds(moduleIds) + '.'));
        }

        struct ExportOwner {
            const ExtensionServiceExportManifest *service{};
            const ExtensionModuleManifest *ownerModule{};
        };

        using ModuleIndex = std::map<std::string, const ExtensionModuleManifest *, std::less<>>;
        using ExportIndex = std::map<std::string, ExportOwner, std::less<>>;
        using DependencyIndex = std::map<std::string, std::set<std::string, std::less<>>, std::less<>>;

        [[nodiscard]] bool HasRole(const ExtensionModuleManifest &moduleManifest, const ExtensionModuleRole role) {
            return std::ranges::find(moduleManifest.roles, role) != moduleManifest.roles.end();
        }

        [[nodiscard]] Result<ModuleIndex> ValidateAndIndexModules(const ExtensionManifest &manifest) {
            const ExtensionManifestLimits limits;
            if (manifest.modules.empty() || manifest.modules.size() > limits.maximumModules)
                return Result<ModuleIndex>::Failure(
                    MakeError(ExtensionErrors::ModuleResolutionFailed, "Module count is outside the supported package bounds."));
            if (manifest.contributions.size() > limits.maximumContributions)
                return Result<ModuleIndex>::Failure(
                    MakeError(ExtensionErrors::ModuleResolutionFailed, "Contribution count exceeds the supported package bound."));

            ModuleIndex modules;
            for (const ExtensionModuleManifest &moduleManifest : manifest.modules) {
                if (moduleManifest.roles.empty())
                    return Result<ModuleIndex>::Failure(
                        MakeError(ExtensionErrors::ModuleResolutionFailed,
                                  "A module has no explicit role. Involved modules: " + moduleManifest.id + '.'));
                if (moduleManifest.roles.size() > 5 || moduleManifest.dependencies.size() > limits.maximumModules ||
                    moduleManifest.exports.size() > limits.maximumContributions ||
                    moduleManifest.imports.size() > limits.maximumContributions)
                    return Result<ModuleIndex>::Failure(
                        MakeError(ExtensionErrors::ModuleResolutionFailed,
                                  "Module metadata exceeds a bounded count. Involved modules: " + moduleManifest.id + '.'));
                if (!moduleManifest.exports.empty() && !HasRole(moduleManifest, ExtensionModuleRole::BackendCapability))
                    return Result<ModuleIndex>::Failure(
                        MakeError(ExtensionErrors::ModuleResolutionFailed,
                                  "A service export requires backend-capability authority. Involved modules: " + moduleManifest.id + '.'));
                if (!modules.try_emplace(moduleManifest.id, &moduleManifest).second)
                    return Result<ModuleIndex>::Failure(
                        MakeError(ExtensionErrors::ModuleResolutionFailed,
                                  "A module identity is duplicated. Involved modules: " + moduleManifest.id + '.'));
            }
            return Result<ModuleIndex>::Success(std::move(modules));
        }

        [[nodiscard]] Result<void> ValidateContributionOwners(const ExtensionManifest &manifest, const ModuleIndex &modules) {
            std::set<std::string, std::less<>> identities;
            for (const ExtensionContributionManifest &contribution : manifest.contributions) {
                const auto owner = modules.find(contribution.owningModule);
                if (owner == modules.end())
                    return Result<void>::Failure(
                        MakeError(ExtensionErrors::ModuleResolutionFailed,
                                  "A contribution references an absent owner. Involved modules: " + contribution.owningModule + '.'));
                if (!identities.insert(contribution.id).second)
                    return Result<void>::Failure(
                        MakeError(ExtensionErrors::ModuleResolutionFailed,
                                  "A contribution identity is duplicated. Involved modules: " + contribution.owningModule + '.'));
                if (contribution.type == "asset.importer" && !HasRole(*owner->second, ExtensionModuleRole::BackendCapability))
                    return Result<void>::Failure(MakeError(ExtensionErrors::ModuleResolutionFailed,
                                                           "An asset importer requires backend-capability authority. Involved modules: " +
                                                               contribution.owningModule + '.'));
                if (contribution.type == "editor.panel" && !HasRole(*owner->second, ExtensionModuleRole::EditorPresentation))
                    return Result<void>::Failure(MakeError(ExtensionErrors::ModuleResolutionFailed,
                                                           "An editor panel requires editor-presentation authority. Involved modules: " +
                                                               contribution.owningModule + '.'));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<ExportIndex> IndexExports(const ExtensionManifest &manifest) {
            ExportIndex exports;
            for (const ExtensionModuleManifest &moduleManifest : manifest.modules) {
                for (const ExtensionServiceExportManifest &service : moduleManifest.exports) {
                    const auto [position, inserted] = exports.try_emplace(service.id, ExportOwner{&service, &moduleManifest});
                    if (!inserted)
                        return Result<ExportIndex>::Failure(
                            MakeError(ExtensionErrors::ModuleResolutionFailed,
                                      "Duplicate service export '" + service.id +
                                          "'. Involved modules: " + std::min(position->second.ownerModule->id, moduleManifest.id) + ", " +
                                          std::max(position->second.ownerModule->id, moduleManifest.id) + '.'));
                }
            }
            return Result<ExportIndex>::Success(std::move(exports));
        }

        [[nodiscard]] Result<void> AddDeclaredDependencies(const ExtensionManifest &manifest, const ModuleIndex &modules,
                                                           DependencyIndex &dependencies) {
            for (const ExtensionModuleManifest &moduleManifest : manifest.modules) {
                auto &moduleDependencies = dependencies[moduleManifest.id];
                for (const std::string &dependency : moduleManifest.dependencies) {
                    if (!modules.contains(dependency))
                        return Result<void>::Failure(
                            MakeError(ExtensionErrors::ModuleResolutionFailed, "Missing local module dependency '" + dependency +
                                                                                   "'. Involved modules: " + moduleManifest.id + '.'));
                    if (dependency == moduleManifest.id)
                        return Result<void>::Failure(
                            MakeError(ExtensionErrors::ModuleResolutionFailed,
                                      "A module cannot depend on itself. Involved modules: " + moduleManifest.id + '.'));
                    moduleDependencies.insert(dependency);
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AddServiceDependencies(const ExtensionManifest &manifest, const ExportIndex &exports,
                                                          DependencyIndex &dependencies) {
            for (const ExtensionModuleManifest &moduleManifest : manifest.modules) {
                for (const ExtensionServiceImportManifest &serviceImport : moduleManifest.imports) {
                    const auto found = exports.find(serviceImport.service);
                    if (found == exports.end())
                        return Result<void>::Failure(
                            MakeError(ExtensionErrors::ModuleResolutionFailed, "Missing service export '" + serviceImport.service +
                                                                                   "'. Involved modules: " + moduleManifest.id + '.'));
                    const ExportOwner &provider = found->second;
                    if (provider.service->contract != serviceImport.contract ||
                        !IsCompatibleVersion(provider.service->version, serviceImport.minimumVersion)) {
                        const std::set<std::string, std::less<>> involved{moduleManifest.id, provider.ownerModule->id};
                        return Result<void>::Failure(MakeError(ExtensionErrors::ModuleResolutionFailed,
                                                               "Incompatible service export '" + serviceImport.service +
                                                                   "'. Involved modules: " + JoinModuleIds(involved) + '.'));
                    }
                    if (provider.ownerModule->id == moduleManifest.id)
                        return Result<void>::Failure(
                            MakeError(ExtensionErrors::ModuleResolutionFailed,
                                      "A module cannot import its own service. Involved modules: " + moduleManifest.id + '.'));
                    dependencies[moduleManifest.id].insert(provider.ownerModule->id);
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::vector<std::string>> SortModules(const DependencyIndex &dependencies) {
            DependencyIndex remaining = dependencies;
            std::vector<std::string> order;
            order.reserve(remaining.size());
            while (!remaining.empty()) {
                const auto ready = std::ranges::find_if(remaining, [](const auto &entry) {
                    return entry.second.empty();
                });
                if (ready == remaining.end()) {
                    std::set<std::string, std::less<>> involved;
                    for (const auto &[moduleId, moduleDependencies] : remaining) {
                        involved.insert(moduleId);
                        involved.insert(moduleDependencies.begin(), moduleDependencies.end());
                    }
                    return Result<std::vector<std::string>>::Failure(
                        MakeError(ExtensionErrors::ModuleResolutionFailed,
                                  "The local module dependency graph contains a cycle. Involved modules: " + JoinModuleIds(involved) +
                                      '.'));
                }
                const std::string moduleId = ready->first;
                order.push_back(moduleId);
                remaining.erase(ready);
                for (auto &[candidate, candidateDependencies] : remaining) {
                    static_cast<void>(candidate);
                    candidateDependencies.erase(moduleId);
                }
            }
            return Result<std::vector<std::string>>::Success(std::move(order));
        }

        [[nodiscard]] Result<void> ValidateHeadlessDependencies(const DependencyIndex &dependencies, const ModuleIndex &modules) {
            for (const auto &[moduleId, moduleDependencies] : dependencies) {
                if (IsPresentationOnly(*modules.at(moduleId)))
                    continue;
                for (const std::string &dependency : moduleDependencies) {
                    if (IsPresentationOnly(*modules.at(dependency))) {
                        const std::set<std::string, std::less<>> involved{moduleId, dependency};
                        return Result<void>::Failure(
                            MakeError(ExtensionErrors::ModuleResolutionFailed,
                                      "A headless module requires an excluded presentation-only module. Involved modules: " +
                                          JoinModuleIds(involved) + '.'));
                    }
                }
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ResolveExtensionModules */
    Result<ExtensionModulePlan> ResolveExtensionModules(const ExtensionManifest &manifest, const ExtensionHostProfile profile) {
        auto modulesResult = ValidateAndIndexModules(manifest);
        if (modulesResult.HasError())
            return Result<ExtensionModulePlan>::Failure(modulesResult.ErrorValue());
        const ModuleIndex &modules = modulesResult.Value();
        if (auto contributionResult = ValidateContributionOwners(manifest, modules); contributionResult.HasError())
            return Result<ExtensionModulePlan>::Failure(contributionResult.ErrorValue());

        auto exportsResult = IndexExports(manifest);
        if (exportsResult.HasError())
            return Result<ExtensionModulePlan>::Failure(exportsResult.ErrorValue());

        DependencyIndex dependencies;
        if (auto result = AddDeclaredDependencies(manifest, modules, dependencies); result.HasError())
            return Result<ExtensionModulePlan>::Failure(result.ErrorValue());
        if (auto result = AddServiceDependencies(manifest, exportsResult.Value(), dependencies); result.HasError())
            return Result<ExtensionModulePlan>::Failure(result.ErrorValue());

        auto orderResult = SortModules(dependencies);
        if (orderResult.HasError())
            return Result<ExtensionModulePlan>::Failure(orderResult.ErrorValue());
        std::vector<std::string> order = std::move(orderResult).Value();

        if (profile == ExtensionHostProfile::Headless) {
            if (auto headlessResult = ValidateHeadlessDependencies(dependencies, modules); headlessResult.HasError())
                return Result<ExtensionModulePlan>::Failure(headlessResult.ErrorValue());
            std::erase_if(order, [&modules](const std::string &moduleId) {
                return IsPresentationOnly(*modules.at(moduleId));
            });
        }

        ExtensionModulePlan plan{.moduleIds = std::move(order)};
        const std::set<std::string, std::less<>> selected(plan.moduleIds.begin(), plan.moduleIds.end());
        for (const ExtensionContributionManifest &contribution : manifest.contributions) {
            if (selected.contains(contribution.owningModule))
                plan.contributions.push_back(contribution);
        }
        std::ranges::sort(plan.contributions, {}, &ExtensionContributionManifest::id);
        return Result<ExtensionModulePlan>::Success(std::move(plan));
    }
}  // namespace Horo::Extensions
