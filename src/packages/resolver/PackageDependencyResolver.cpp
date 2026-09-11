#include "Horo/Packages/PackageDependencyResolver.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace Horo::Packages {
    namespace {
        const ErrorDomainId ResolverDomain{"packages.resolver"};
        const ErrorCodeDescriptor InvalidInput{ResolverDomain, ErrorCode{"packages.resolver.invalid_input"}, ErrorSeverity::Error,
                                               "Package resolver input is invalid.",
                                               "Provide canonical bounded package candidates and requests."};
        const ErrorCodeDescriptor ResourceLimit{ResolverDomain, ErrorCode{"packages.resolver.limit"}, ErrorSeverity::Error,
                                                "Package resolver input exceeds its resource policy.",
                                                "Reduce candidate, dependency, feature, package, graph depth, or search-step counts."};
        const ErrorCodeDescriptor Unsatisfied{ResolverDomain, ErrorCode{"packages.resolver.unsatisfied"}, ErrorSeverity::Error,
                                              "No package candidate satisfies the request.",
                                              "Add a compatible package version or revise the dependency request."};
        const ErrorCodeDescriptor Conflict{ResolverDomain, ErrorCode{"packages.resolver.conflict"}, ErrorSeverity::Error,
                                           "Package version constraints conflict.", "Align transitive dependency version requirements."};
        const ErrorCodeDescriptor SourceAmbiguity{ResolverDomain, ErrorCode{"packages.resolver.source_ambiguity"}, ErrorSeverity::Error,
                                                  "Package sources disagree about an exact package identity.",
                                                  "Resolve the source digest conflict before retrying."};
        const ErrorCodeDescriptor Cycle{ResolverDomain, ErrorCode{"packages.resolver.cycle"}, ErrorSeverity::Error,
                                        "Package dependency graph contains a cycle.", "Remove the reported circular dependency."};

        [[nodiscard]] bool CanonicalToken(const std::string_view text, const std::size_t maximum) noexcept {
            if (text.empty() || text.size() > maximum || text.front() == '.' || text.back() == '.' ||
                text.find("..") != std::string_view::npos)
                return false;
            return std::ranges::all_of(text, [](const unsigned char value) {
                return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_';
            });
        }

        [[nodiscard]] std::optional<std::uint32_t> ParseNumber(const std::string_view text) noexcept {
            if (text.empty() || (text.size() > 1U && text.front() == '0'))
                return std::nullopt;
            std::uint32_t value{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        [[nodiscard]] bool CanonicalPrerelease(std::string_view text) noexcept {
            if (text.empty() || text.size() > 64U)
                return false;
            while (!text.empty()) {
                const std::size_t separator = text.find('.');
                const std::string_view identifier = text.substr(0, separator);
                if (identifier.empty() || !std::ranges::all_of(identifier, [](const unsigned char value) {
                    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
                           value == '-';
                }))
                    return false;
                const bool numeric = std::ranges::all_of(identifier, [](const unsigned char value) {
                    return value >= '0' && value <= '9';
                });
                if (numeric && identifier.size() > 1U && identifier.front() == '0')
                    return false;
                if (separator == std::string_view::npos)
                    return true;
                text.remove_prefix(separator + 1U);
            }
            return false;
        }

        [[nodiscard]] std::strong_ordering ComparePrerelease(std::string_view left, std::string_view right) noexcept {
            while (!left.empty() && !right.empty()) {
                const std::size_t leftSeparator = left.find('.');
                const std::size_t rightSeparator = right.find('.');
                const std::string_view leftId = left.substr(0, leftSeparator);
                const std::string_view rightId = right.substr(0, rightSeparator);
                const bool leftNumeric = std::ranges::all_of(leftId, [](const unsigned char value) {
                    return value >= '0' && value <= '9';
                });
                const bool rightNumeric = std::ranges::all_of(rightId, [](const unsigned char value) {
                    return value >= '0' && value <= '9';
                });
                if (leftNumeric != rightNumeric)
                    return leftNumeric ? std::strong_ordering::less : std::strong_ordering::greater;
                if (leftNumeric && leftId.size() != rightId.size())
                    return leftId.size() < rightId.size() ? std::strong_ordering::less : std::strong_ordering::greater;
                if (const auto comparison = leftId <=> rightId; comparison != 0)
                    return comparison;
                if (leftSeparator == std::string_view::npos || rightSeparator == std::string_view::npos) {
                    if (leftSeparator == rightSeparator)
                        return std::strong_ordering::equal;
                    return leftSeparator == std::string_view::npos ? std::strong_ordering::less : std::strong_ordering::greater;
                }
                left.remove_prefix(leftSeparator + 1U);
                right.remove_prefix(rightSeparator + 1U);
            }
            return std::strong_ordering::equal;
        }

        [[nodiscard]] bool Compatible(const PackageResolutionCandidate &candidate, const PackagePlatform &host) {
            return candidate.platforms.empty() || std::ranges::find(candidate.platforms, host) != candidate.platforms.end();
        }

        [[nodiscard]] bool ContainsFeatures(const PackageResolutionCandidate &candidate,
                                            const std::set<std::string, std::less<>> &required) {
            return std::ranges::all_of(required, [&](const std::string &feature) {
                return std::ranges::find(candidate.features, feature) != candidate.features.end();
            });
        }

        [[nodiscard]] bool ValidVersion(const PackageVersion &version) {
            const auto parsed = PackageVersion::Parse(version.ToString());
            return parsed.HasValue() && parsed.Value() == version;
        }

        [[nodiscard]] bool ValidRange(const PackageVersionRange &range) {
            switch (range.kind) {
                case PackageVersionRange::Kind::Any:
                    return true;
                case PackageVersionRange::Kind::Exact:
                case PackageVersionRange::Kind::Caret:
                    return ValidVersion(range.version);
            }
            return false;
        }

        [[nodiscard]] bool ValidPlatform(const PackagePlatform &platform) {
            return CanonicalToken(platform.operatingSystem, 64U) && CanonicalToken(platform.architecture, 64U) &&
                   CanonicalToken(platform.sdkAbi, 128U);
        }

        [[nodiscard]] bool ValidDependency(const PackageDependencyRequest &dependency) {
            const bool validRequirement = dependency.requirement == PackageDependencyRequirement::Required ||
                                          dependency.requirement == PackageDependencyRequirement::Optional;
            return validRequirement && ValidRange(dependency.versions) &&
                   std::ranges::all_of(dependency.requiredFeatures, [](const std::string &feature) {
                return CanonicalToken(feature, 128U);
            });
        }

        struct Constraints {
            std::vector<PackageVersionRange> ranges;
            std::set<std::string, std::less<>> features;
        };

        struct SearchState {
            std::map<std::string, Constraints, std::less<>> constraints;
            std::map<std::string, std::size_t, std::less<>> selected;
        };

        struct Failure {
            const ErrorCodeDescriptor *descriptor{&Unsatisfied};
            std::string message;
            int priority{};
        };

        void KeepFailure(Failure &target, const ErrorCodeDescriptor &descriptor, std::string message, const int priority) {
            if (priority > target.priority || (priority == target.priority && (target.message.empty() || message < target.message)))
                target = Failure{&descriptor, std::move(message), priority};
        }

        [[nodiscard]] bool Matches(const PackageResolutionCandidate &candidate, const Constraints &constraints,
                                   const PackagePlatform &host) {
            return !candidate.yanked && Compatible(candidate, host) && ContainsFeatures(candidate, constraints.features) &&
                   std::ranges::all_of(constraints.ranges, [&](const PackageVersionRange &range) {
                return range.Allows(candidate.version);
            });
        }

        [[nodiscard]] bool HasCycle(const PackageResolutionRequest &request, const SearchState &state, std::string &explanation) {
            enum class Mark : std::uint8_t {
                Visiting,
                Visited
            };
            std::map<std::string, Mark, std::less<>> marks;
            std::vector<std::string> path;
            const auto visit = [&](const auto &self, const std::string &id) -> bool {
                if (const auto mark = marks.find(id); mark != marks.end()) {
                    if (mark->second == Mark::Visited)
                        return false;
                    const auto begin = std::ranges::find(path, id);
                    explanation = "Dependency cycle: ";
                    for (auto it = begin; it != path.end(); ++it)
                        explanation += (it == begin ? "" : " -> ") + *it;
                    explanation += " -> " + id;
                    return true;
                }
                marks.emplace(id, Mark::Visiting);
                path.push_back(id);
                const auto selected = state.selected.find(id);
                if (selected != state.selected.end()) {
                    for (const PackageDependencyRequest &dependency : request.candidates[selected->second].dependencies) {
                        if (dependency.requirement == PackageDependencyRequirement::Optional && !request.includeOptionalDependencies)
                            continue;
                        if (self(self, dependency.package.Value()))
                            return true;
                    }
                }
                path.pop_back();
                marks[id] = Mark::Visited;
                return false;
            };
            for (const auto &[id, unused] : state.selected) {
                static_cast<void>(unused);
                if (!marks.contains(id) && visit(visit, id))
                    return true;
            }
            return false;
        }

        [[nodiscard]] std::vector<std::size_t> MatchingCandidates(const PackageResolutionRequest &request, const SearchState &state,
                                                                  const std::string &unresolved) {
            std::vector<std::size_t> matches;
            for (std::size_t index = 0; index < request.candidates.size(); ++index) {
                const PackageResolutionCandidate &candidate = request.candidates[index];
                if (candidate.package.Value() == unresolved && Matches(candidate, state.constraints.at(unresolved), request.host))
                    matches.push_back(index);
            }
            std::ranges::sort(matches, [&](const std::size_t lhs, const std::size_t rhs) {
                const auto &left = request.candidates[lhs];
                const auto &right = request.candidates[rhs];
                if (left.sourceRank != right.sourceRank)
                    return left.sourceRank < right.sourceRank;
                if (left.version != right.version)
                    return left.version > right.version;
                return left.sourceId < right.sourceId;
            });
            return matches;
        }

        [[nodiscard]] std::optional<PackageVersion> FindSourceAmbiguity(const PackageResolutionRequest &request,
                                                                        const std::vector<std::size_t> &matches) {
            std::map<PackageVersion, std::string> digests;
            for (const std::size_t index : matches) {
                const PackageResolutionCandidate &candidate = request.candidates[index];
                const auto [existing, inserted] = digests.emplace(candidate.version, candidate.artifactDigest);
                if (!inserted && existing->second != candidate.artifactDigest)
                    return candidate.version;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool AddDependencies(const PackageResolutionRequest &request, const PackageResolutionCandidate &candidate,
                                           SearchState &branch, Failure &failure) {
            for (const PackageDependencyRequest &dependency : candidate.dependencies) {
                if (dependency.requirement == PackageDependencyRequirement::Optional && !request.includeOptionalDependencies)
                    continue;
                Constraints &next = branch.constraints[dependency.package.Value()];
                next.ranges.push_back(dependency.versions);
                next.features.insert(dependency.requiredFeatures.begin(), dependency.requiredFeatures.end());
                if (branch.constraints.size() > request.limits.packages) {
                    KeepFailure(failure, ResourceLimit, "Resolved package count exceeds its limit.", 5);
                    return false;
                }
                if (const auto selected = branch.selected.find(dependency.package.Value());
                    selected != branch.selected.end() && !Matches(request.candidates[selected->second], next, request.host)) {
                    KeepFailure(failure, Conflict, "Conflicting constraints for " + dependency.package.Value(), 2);
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ResolveNext(const PackageResolutionRequest &request, SearchState state, SearchState &solution, Failure &failure,
                                       std::size_t depth, std::size_t &searchSteps);

        [[nodiscard]] bool TryCandidates(const PackageResolutionRequest &request, const SearchState &state, SearchState &solution,
                                         Failure &failure, const std::string &unresolved, const std::vector<std::size_t> &matches,
                                         const std::size_t depth, std::size_t &searchSteps) {
            for (const std::size_t candidateIndex : matches) {
                const PackageResolutionCandidate &candidate = request.candidates[candidateIndex];
                SearchState branch = state;
                branch.selected[unresolved] = candidateIndex;
                if (!AddDependencies(request, candidate, branch, failure))
                    continue;
                std::string cycle;
                if (HasCycle(request, branch, cycle)) {
                    KeepFailure(failure, Cycle, std::move(cycle), 4);
                    continue;
                }
                if (ResolveNext(request, std::move(branch), solution, failure, depth + 1U, searchSteps))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool ResolveNext(const PackageResolutionRequest &request, SearchState state, SearchState &solution, Failure &failure,
                                       const std::size_t depth, std::size_t &searchSteps) {
            if (searchSteps >= request.limits.searchSteps) {
                KeepFailure(failure, ResourceLimit, "Dependency search exceeds its exploration-step limit.", 5);
                return false;
            }
            ++searchSteps;
            if (depth > request.limits.graphDepth) {
                KeepFailure(failure, ResourceLimit, "Dependency graph exceeds maximum depth.", 5);
                return false;
            }
            const auto unresolved = std::ranges::find_if(state.constraints, [&](const auto &entry) {
                return !state.selected.contains(entry.first);
            });
            if (unresolved == state.constraints.end()) {
                solution = std::move(state);
                return true;
            }
            const std::vector<std::size_t> matches = MatchingCandidates(request, state, unresolved->first);
            if (!matches.empty()) {
                if (const auto ambiguous = FindSourceAmbiguity(request, matches); ambiguous.has_value()) {
                    KeepFailure(failure, SourceAmbiguity, "Source ambiguity for " + unresolved->first + "@" + ambiguous->ToString(), 6);
                    return false;
                }
                return TryCandidates(request, state, solution, failure, unresolved->first, matches, depth, searchSteps);
            }
            const bool conflicting = unresolved->second.ranges.size() > 1U;
            KeepFailure(failure, conflicting ? Conflict : Unsatisfied,
                        std::string{conflicting ? "Conflicting constraints for " : "No compatible candidate for "} + unresolved->first,
                        conflicting ? 2 : 1);
            return false;
        }

        [[nodiscard]] Result<void> Validate(const PackageResolutionRequest &request) {
            if (request.roots.empty())
                return Result<void>::Failure(MakeError(InvalidInput, "At least one root dependency is required."));
            if (request.candidates.size() > request.limits.candidates || request.roots.size() > request.limits.packages)
                return Result<void>::Failure(MakeError(ResourceLimit));
            std::set<std::pair<std::string, std::string>> identities;
            for (const PackageResolutionCandidate &candidate : request.candidates) {
                if (!ValidVersion(candidate.version) || !CanonicalToken(candidate.sourceId, 128U) || candidate.artifactDigest.empty() ||
                    candidate.artifactDigest.size() > 128U || candidate.dependencies.size() > request.limits.dependenciesPerCandidate ||
                    candidate.features.size() > request.limits.featuresPerCandidate ||
                    !identities.emplace(candidate.package.Value(), candidate.version.ToString() + "@" + candidate.sourceId).second)
                    return Result<void>::Failure(MakeError(InvalidInput));
                if (!std::ranges::all_of(candidate.features, [](const std::string &feature) {
                    return CanonicalToken(feature, 128U);
                }))
                    return Result<void>::Failure(MakeError(InvalidInput));
                std::set<std::string, std::less<>> uniqueFeatures;
                for (const std::string &feature : candidate.features) {
                    if (!uniqueFeatures.insert(feature).second)
                        return Result<void>::Failure(MakeError(InvalidInput));
                }
                std::set<std::string, std::less<>> uniqueDependencies;
                for (const PackageDependencyRequest &dependency : candidate.dependencies) {
                    if (!uniqueDependencies.insert(dependency.package.Value()).second || !ValidDependency(dependency))
                        return Result<void>::Failure(MakeError(InvalidInput));
                }
                if (!std::ranges::all_of(candidate.platforms, ValidPlatform))
                    return Result<void>::Failure(MakeError(InvalidInput));
            }
            for (const PackageDependencyRequest &root : request.roots) {
                if (!ValidDependency(root))
                    return Result<void>::Failure(MakeError(InvalidInput));
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc HoroPackageId::Parse */
    Result<HoroPackageId> HoroPackageId::Parse(const std::string_view text) {
        if (!CanonicalToken(text, 255U))
            return Result<HoroPackageId>::Failure(MakeError(InvalidInput, "Package ID is not canonical."));
        return Result<HoroPackageId>::Success(HoroPackageId{std::string{text}});
    }

    /** @copydoc HoroPackageId::HoroPackageId */
    HoroPackageId::HoroPackageId(std::string value) : value_(std::move(value)) {}

    /** @copydoc HoroPackageId::Value */
    const std::string &HoroPackageId::Value() const noexcept {
        return value_;
    }

    /** @copydoc PackageVersion::Parse */
    Result<PackageVersion> PackageVersion::Parse(const std::string_view text) {
        const std::size_t first = text.find('.');
        const std::size_t second = first == std::string_view::npos ? first : text.find('.', first + 1U);
        const std::size_t dash = second == std::string_view::npos ? second : text.find('-', second + 1U);
        const std::string_view core = text.substr(0, dash);
        if (first == std::string_view::npos || second == std::string_view::npos || core.find('.', second + 1U) != std::string_view::npos)
            return Result<PackageVersion>::Failure(MakeError(InvalidInput, "Semantic version is not canonical."));
        const auto major = ParseNumber(text.substr(0, first));
        const auto minor = ParseNumber(text.substr(first + 1U, second - first - 1U));
        const auto patch = ParseNumber(text.substr(second + 1U, dash - second - 1U));
        const std::string_view prerelease = dash == std::string_view::npos ? std::string_view{} : text.substr(dash + 1U);
        if (!major || !minor || !patch || (dash != std::string_view::npos && !CanonicalPrerelease(prerelease)))
            return Result<PackageVersion>::Failure(MakeError(InvalidInput, "Semantic version is not canonical."));
        return Result<PackageVersion>::Success(PackageVersion{*major, *minor, *patch, std::string{prerelease}});
    }

    /** @copydoc PackageVersion::ToString */
    std::string PackageVersion::ToString() const {
        std::string result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        if (!prerelease.empty())
            result += "-" + prerelease;
        return result;
    }

    std::strong_ordering PackageVersion::operator<=>(const PackageVersion &other) const noexcept {
        if (const auto value = major <=> other.major; value != 0)
            return value;
        if (const auto value = minor <=> other.minor; value != 0)
            return value;
        if (const auto value = patch <=> other.patch; value != 0)
            return value;
        if (prerelease.empty() != other.prerelease.empty())
            return prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
        return ComparePrerelease(prerelease, other.prerelease);
    }

    /** @copydoc PackageVersionRange::Allows */
    bool PackageVersionRange::Allows(const PackageVersion &candidate) const noexcept {
        if (kind == Kind::Any)
            return true;
        if (kind == Kind::Exact)
            return candidate == version;
        if (!candidate.prerelease.empty() && (version.prerelease.empty() || candidate.major != version.major ||
                                              candidate.minor != version.minor || candidate.patch != version.patch))
            return false;
        if (candidate < version)
            return false;
        if (version.major > 0U)
            return candidate.major == version.major;
        if (version.minor > 0U)
            return candidate.major == 0U && candidate.minor == version.minor;
        return candidate.major == 0U && candidate.minor == 0U && candidate.patch == version.patch;
    }

    /** @copydoc PackageVersionRange::ToString */
    std::string PackageVersionRange::ToString() const {
        if (kind == Kind::Any)
            return "*";
        return std::string{kind == Kind::Caret ? "^" : ""} + version.ToString();
    }

    /** @copydoc PackageDependencyResolver::Resolve */
    Result<PackageResolutionPlan> PackageDependencyResolver::Resolve(const PackageResolutionRequest &request) {
        if (const auto validation = Validate(request); validation.HasError())
            return Result<PackageResolutionPlan>::Failure(validation.ErrorValue());
        SearchState initial;
        for (const PackageDependencyRequest &root : request.roots) {
            Constraints &constraints = initial.constraints[root.package.Value()];
            constraints.ranges.push_back(root.versions);
            constraints.features.insert(root.requiredFeatures.begin(), root.requiredFeatures.end());
        }
        SearchState solution;
        Failure failure;
        std::size_t searchSteps{};
        if (!ResolveNext(request, std::move(initial), solution, failure, 0U, searchSteps))
            return Result<PackageResolutionPlan>::Failure(MakeError(*failure.descriptor, std::move(failure.message)));

        PackageResolutionPlan plan;
        plan.packages.reserve(solution.selected.size());
        for (const auto &[id, index] : solution.selected) {
            static_cast<void>(id);
            const PackageResolutionCandidate &candidate = request.candidates[index];
            ResolvedPackage resolved{candidate.package, candidate.version, candidate.sourceId, candidate.artifactDigest, {}};
            for (const PackageDependencyRequest &dependency : candidate.dependencies) {
                if (dependency.requirement == PackageDependencyRequirement::Required || request.includeOptionalDependencies)
                    resolved.dependencies.push_back(dependency.package);
            }
            std::ranges::sort(resolved.dependencies, [](const HoroPackageId &left, const HoroPackageId &right) {
                return left.Value() < right.Value();
            });
            plan.packages.push_back(std::move(resolved));
        }
        return Result<PackageResolutionPlan>::Success(std::move(plan));
    }
}  // namespace Horo::Packages
