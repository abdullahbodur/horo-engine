#include "Horo/Packages/PackageDependencyResolver.h"

#include <algorithm>
#include <charconv>
#include <format>
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
                if (const bool numeric = std::ranges::all_of(identifier,
                                                             [](const unsigned char value) {
                    return value >= '0' && value <= '9';
                });
                    numeric && identifier.size() > 1U && identifier.front() == '0')
                    return false;
                if (separator == std::string_view::npos)
                    return true;
                text.remove_prefix(separator + 1U);
            }
            return false;
        }

        struct PrereleaseIdentifier {
            std::string_view value;
            std::size_t separator;
            bool numeric;
        };

        [[nodiscard]] PrereleaseIdentifier FirstPrereleaseIdentifier(const std::string_view text) noexcept {
            const std::size_t separator = text.find('.');
            const std::string_view value = text.substr(0, separator);
            return {value, separator, std::ranges::all_of(value, [](const unsigned char character) {
                return character >= '0' && character <= '9';
            })};
        }

        [[nodiscard]] std::strong_ordering ComparePrereleaseIdentifier(const PrereleaseIdentifier &left,
                                                                       const PrereleaseIdentifier &right) noexcept {
            if (left.numeric != right.numeric)
                return left.numeric ? std::strong_ordering::less : std::strong_ordering::greater;
            if (left.numeric && left.value.size() != right.value.size())
                return left.value.size() < right.value.size() ? std::strong_ordering::less : std::strong_ordering::greater;
            return left.value <=> right.value;
        }

        [[nodiscard]] std::strong_ordering ComparePrerelease(std::string_view left, std::string_view right) noexcept {
            while (!left.empty() && !right.empty()) {
                const PrereleaseIdentifier leftIdentifier = FirstPrereleaseIdentifier(left);
                const PrereleaseIdentifier rightIdentifier = FirstPrereleaseIdentifier(right);
                if (const auto comparison = ComparePrereleaseIdentifier(leftIdentifier, rightIdentifier); comparison != 0)
                    return comparison;
                if (leftIdentifier.separator == std::string_view::npos || rightIdentifier.separator == std::string_view::npos) {
                    if (leftIdentifier.separator == rightIdentifier.separator)
                        return std::strong_ordering::equal;
                    return leftIdentifier.separator == std::string_view::npos ? std::strong_ordering::less : std::strong_ordering::greater;
                }
                left.remove_prefix(leftIdentifier.separator + 1U);
                right.remove_prefix(rightIdentifier.separator + 1U);
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
            using enum PackageVersionRange::Kind;
            switch (range.kind) {
                case Any:
                    return true;
                case Exact:
                case Caret:
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

        struct SearchContext {
            const PackageResolutionRequest &request;
            SearchState &solution;
            Failure &failure;
            std::size_t searchSteps{};
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

        enum class VisitMark : std::uint8_t {
            Visiting,
            Visited,
        };

        struct CycleDetector {
            const PackageResolutionRequest &request;
            const SearchState &state;
            std::string &explanation;
            std::map<std::string, VisitMark, std::less<>> marks;
            std::vector<std::string> path;

            void RecordCycle(const std::string &id) {
                const auto begin = std::ranges::find(path, id);
                explanation = "Dependency cycle: ";
                for (auto it = begin; it != path.end(); ++it)
                    explanation += (it == begin ? "" : " -> ") + *it;
                explanation += " -> " + id;
            }

            [[nodiscard]] bool Visit(const std::string &id) {
                if (const auto mark = marks.find(id); mark != marks.end()) {
                    if (mark->second != VisitMark::Visiting)
                        return false;
                    RecordCycle(id);
                    return true;
                }
                marks.try_emplace(id, VisitMark::Visiting);
                path.push_back(id);
                bool found{};
                if (const auto selected = state.selected.find(id); selected != state.selected.end()) {
                    found = std::ranges::any_of(request.candidates[selected->second].dependencies,
                                                [&](const PackageDependencyRequest &dependency) {
                        return (dependency.requirement == PackageDependencyRequirement::Required || request.includeOptionalDependencies) &&
                               Visit(dependency.package.Value());
                    });
                }
                if (found)
                    return true;
                path.pop_back();
                marks[id] = VisitMark::Visited;
                return false;
            }
        };

        [[nodiscard]] bool HasCycle(const PackageResolutionRequest &request, const SearchState &state, std::string &explanation) {
            CycleDetector detector{request, state, explanation};
            return std::ranges::any_of(state.selected, [&](const auto &selection) {
                return detector.Visit(selection.first);
            });
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
                return left.source.Value() < right.source.Value();
            });
            return matches;
        }

        [[nodiscard]] std::optional<PackageVersion> FindSourceAmbiguity(const PackageResolutionRequest &request,
                                                                        const std::vector<std::size_t> &matches) {
            std::map<PackageVersion, Sha256Digest> digests;
            for (const std::size_t index : matches) {
                const PackageResolutionCandidate &candidate = request.candidates[index];
                const auto [existing, inserted] = digests.try_emplace(candidate.version, candidate.artifactDigest);
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

        [[nodiscard]] bool ResolveNext(SearchContext &context, SearchState state, std::size_t depth);

        [[nodiscard]] bool TryCandidates(SearchContext &context, const SearchState &state, const std::string &unresolved,
                                         const std::vector<std::size_t> &matches, const std::size_t depth) {
            for (const std::size_t candidateIndex : matches) {
                const PackageResolutionCandidate &candidate = context.request.candidates[candidateIndex];
                SearchState branch = state;
                branch.selected[unresolved] = candidateIndex;
                if (!AddDependencies(context.request, candidate, branch, context.failure))
                    continue;
                if (std::string cycle; HasCycle(context.request, branch, cycle)) {
                    KeepFailure(context.failure, Cycle, std::move(cycle), 4);
                    continue;
                }
                if (ResolveNext(context, std::move(branch), depth + 1U))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool ResolveNext(SearchContext &context, SearchState state, const std::size_t depth) {
            if (context.searchSteps >= context.request.limits.searchSteps) {
                KeepFailure(context.failure, ResourceLimit, "Dependency search exceeds its exploration-step limit.", 5);
                return false;
            }
            ++context.searchSteps;
            if (depth > context.request.limits.graphDepth) {
                KeepFailure(context.failure, ResourceLimit, "Dependency graph exceeds maximum depth.", 5);
                return false;
            }
            const auto unresolved = std::ranges::find_if(state.constraints, [&](const auto &entry) {
                return !state.selected.contains(entry.first);
            });
            if (unresolved == state.constraints.end()) {
                context.solution = std::move(state);
                return true;
            }
            if (const std::vector<std::size_t> matches = MatchingCandidates(context.request, state, unresolved->first); !matches.empty()) {
                if (const auto ambiguous = FindSourceAmbiguity(context.request, matches); ambiguous.has_value()) {
                    KeepFailure(context.failure, SourceAmbiguity, "Source ambiguity for " + unresolved->first + "@" + ambiguous->ToString(),
                                6);
                    return false;
                }
                return TryCandidates(context, state, unresolved->first, matches, depth);
            }
            const bool conflicting = unresolved->second.ranges.size() > 1U;
            KeepFailure(context.failure, conflicting ? Conflict : Unsatisfied,
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
                if (!ValidVersion(candidate.version) || candidate.dependencies.size() > request.limits.dependenciesPerCandidate ||
                    candidate.features.size() > request.limits.featuresPerCandidate ||
                    !identities.emplace(candidate.package.Value(), candidate.version.ToString() + "@" + candidate.source.Value()).second)
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

    /** @copydoc HoroPackageSourceId::Parse */
    Result<HoroPackageSourceId> HoroPackageSourceId::Parse(const std::string_view text) {
        if (!CanonicalToken(text, 128U))
            return Result<HoroPackageSourceId>::Failure(MakeError(InvalidInput, "Package source ID is not canonical."));
        return Result<HoroPackageSourceId>::Success(HoroPackageSourceId{std::string{text}});
    }

    /** @copydoc HoroPackageSourceId::HoroPackageSourceId */
    HoroPackageSourceId::HoroPackageSourceId(std::string value) : value_(std::move(value)) {}

    /** @copydoc HoroPackageSourceId::Value */
    const std::string &HoroPackageSourceId::Value() const noexcept {
        return value_;
    }

    /** @copydoc PackageVersion::Parse */
    Result<PackageVersion> PackageVersion::Parse(const std::string_view text) {
        const std::size_t first = text.find('.');
        const std::size_t second = first == std::string_view::npos ? first : text.find('.', first + 1U);
        const std::size_t dash = second == std::string_view::npos ? second : text.find('-', second + 1U);
        if (const std::string_view core = text.substr(0, dash);
            first == std::string_view::npos || second == std::string_view::npos || core.find('.', second + 1U) != std::string_view::npos)
            return Result<PackageVersion>::Failure(MakeError(InvalidInput, "Semantic version is not canonical."));
        const auto major = ParseNumber(text.substr(0, first));
        const auto minor = ParseNumber(text.substr(first + 1U, second - first - 1U));
        const auto patch = ParseNumber(text.substr(second + 1U, dash - second - 1U));
        const std::string_view prerelease = dash == std::string_view::npos ? std::string_view{} : text.substr(dash + 1U);
        if (!major.has_value() || !minor.has_value() || !patch.has_value() ||
            (dash != std::string_view::npos && !CanonicalPrerelease(prerelease)))
            return Result<PackageVersion>::Failure(MakeError(InvalidInput, "Semantic version is not canonical."));
        return Result<PackageVersion>::Success(PackageVersion{*major, *minor, *patch, std::string{prerelease}});
    }

    /** @copydoc PackageVersion::ToString */
    std::string PackageVersion::ToString() const {
        std::string result = std::format("{}.{}.{}", major, minor, patch);
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
        if (SearchContext context{request, solution, failure}; !ResolveNext(context, std::move(initial), 0U))
            return Result<PackageResolutionPlan>::Failure(MakeError(*failure.descriptor, std::move(failure.message)));

        PackageResolutionPlan plan;
        plan.packages.reserve(solution.selected.size());
        for (const auto &[id, index] : solution.selected) {
            static_cast<void>(id);
            const PackageResolutionCandidate &candidate = request.candidates[index];
            ResolvedPackage resolved{candidate.package, candidate.version, candidate.source, candidate.artifactDigest, {}};
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
