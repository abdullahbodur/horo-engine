#include "Horo/Release/ReleaseVersion.h"

#include "Horo/Release/ReleaseErrors.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace Horo::Release {
    namespace {
        [[nodiscard]] bool IsIdentifierCharacter(const char value) noexcept {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '-';
        }

        [[nodiscard]] bool IsNumeric(const std::string_view value) noexcept {
            return !value.empty() && std::ranges::all_of(value, [](const char character) {
                return character >= '0' && character <= '9';
            });
        }

        [[nodiscard]] bool ValidIdentifiers(const std::string_view value, const bool rejectNumericLeadingZero) noexcept {
            if (value.empty())
                return false;
            for (std::size_t begin = 0;;) {
                const std::size_t end = value.find('.', begin);
                const std::string_view identifier = value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
                if (identifier.empty() || !std::ranges::all_of(identifier, IsIdentifierCharacter) ||
                    (rejectNumericLeadingZero && IsNumeric(identifier) && identifier.size() > 1 && identifier.front() == '0'))
                    return false;
                if (end == std::string_view::npos)
                    return true;
                begin = end + 1;
            }
        }

        [[nodiscard]] Result<std::uint32_t> ParseCoreNumber(const std::string_view value) {
            if (value.empty() || (value.size() > 1 && value.front() == '0'))
                return Result<std::uint32_t>::Failure(MakeError(ReleaseErrors::VersionInvalid));
            std::uint32_t parsed{};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size())
                return Result<std::uint32_t>::Failure(MakeError(ReleaseErrors::VersionInvalid));
            return Result<std::uint32_t>::Success(parsed);
        }

        struct VersionParts final {
            std::string_view core;
            std::string_view prerelease;
            std::string_view build;
        };

        [[nodiscard]] Result<VersionParts> SplitVersion(const std::string_view text) {
            if (text.empty() || text.size() > MaximumReleaseVersionBytes)
                return Result<VersionParts>::Failure(MakeError(ReleaseErrors::VersionInvalid));

            const std::size_t plus = text.find('+');
            if (plus != std::string_view::npos && (plus + 1 == text.size() || text.find('+', plus + 1) != std::string_view::npos))
                return Result<VersionParts>::Failure(MakeError(ReleaseErrors::VersionInvalid));
            const std::string_view withoutBuild = text.substr(0, plus);
            const std::string_view build = plus == std::string_view::npos ? std::string_view{} : text.substr(plus + 1);
            if (!build.empty() && !ValidIdentifiers(build, false))
                return Result<VersionParts>::Failure(MakeError(ReleaseErrors::VersionInvalid));

            const std::size_t dash = withoutBuild.find('-');
            const std::string_view prerelease = dash == std::string_view::npos ? std::string_view{} : withoutBuild.substr(dash + 1);
            if (dash != std::string_view::npos && !ValidIdentifiers(prerelease, true))
                return Result<VersionParts>::Failure(MakeError(ReleaseErrors::VersionInvalid));
            return Result<VersionParts>::Success({withoutBuild.substr(0, dash), prerelease, build});
        }

        [[nodiscard]] Result<std::array<std::uint32_t, 3>> ParseCore(const std::string_view core) {
            const std::size_t firstDot = core.find('.');
            const std::size_t secondDot = firstDot == std::string_view::npos ? firstDot : core.find('.', firstDot + 1);
            if (firstDot == std::string_view::npos || secondDot == std::string_view::npos ||
                core.find('.', secondDot + 1) != std::string_view::npos)
                return Result<std::array<std::uint32_t, 3>>::Failure(MakeError(ReleaseErrors::VersionInvalid));
            auto major = ParseCoreNumber(core.substr(0, firstDot));
            auto minor = ParseCoreNumber(core.substr(firstDot + 1, secondDot - firstDot - 1));
            auto patch = ParseCoreNumber(core.substr(secondDot + 1));
            if (major.HasError() || minor.HasError() || patch.HasError())
                return Result<std::array<std::uint32_t, 3>>::Failure(MakeError(ReleaseErrors::VersionInvalid));
            return Result<std::array<std::uint32_t, 3>>::Success({major.Value(), minor.Value(), patch.Value()});
        }

        [[nodiscard]] std::strong_ordering ComparePrereleaseIdentifier(const std::string_view lhs, const std::string_view rhs) noexcept {
            if (lhs == rhs)
                return std::strong_ordering::equal;
            const bool lhsNumeric = IsNumeric(lhs);
            const bool rhsNumeric = IsNumeric(rhs);
            if (lhsNumeric != rhsNumeric)
                return lhsNumeric ? std::strong_ordering::less : std::strong_ordering::greater;
            if (lhsNumeric && lhs.size() != rhs.size())
                return lhs.size() <=> rhs.size();
            return lhs < rhs ? std::strong_ordering::less : std::strong_ordering::greater;
        }

        [[nodiscard]] std::strong_ordering ComparePrerelease(const std::string_view lhs, const std::string_view rhs) noexcept {
            if (lhs.empty() != rhs.empty())
                return lhs.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
            for (std::size_t lhsBegin = 0, rhsBegin = 0;;) {
                const std::size_t lhsEnd = lhs.find('.', lhsBegin);
                const std::size_t rhsEnd = rhs.find('.', rhsBegin);
                const std::string_view lhsIdentifier =
                    lhs.substr(lhsBegin, lhsEnd == std::string_view::npos ? lhs.size() - lhsBegin : lhsEnd - lhsBegin);
                const std::string_view rhsIdentifier =
                    rhs.substr(rhsBegin, rhsEnd == std::string_view::npos ? rhs.size() - rhsBegin : rhsEnd - rhsBegin);
                if (const std::strong_ordering compared = ComparePrereleaseIdentifier(lhsIdentifier, rhsIdentifier);
                    compared != std::strong_ordering::equal)
                    return compared;
                if (lhsEnd == std::string_view::npos || rhsEnd == std::string_view::npos) {
                    if (lhsEnd == rhsEnd)
                        return std::strong_ordering::equal;
                    return lhsEnd == std::string_view::npos ? std::strong_ordering::less : std::strong_ordering::greater;
                }
                lhsBegin = lhsEnd + 1;
                rhsBegin = rhsEnd + 1;
            }
        }

        [[nodiscard]] bool IsKnownSource(const ReleaseVersionClaimSource source) noexcept {
            return source >= ReleaseVersionClaimSource::Requested && source <= ReleaseVersionClaimSource::PersistentContract;
        }

        [[nodiscard]] bool ValidSourceRevision(const ReleaseSourceRevision &revision) noexcept {
            return !revision.value.empty() && revision.value.size() <= MaximumReleaseSourceRevisionBytes &&
                   std::ranges::all_of(revision.value, [](const unsigned char value) {
                return value >= 0x21 && value <= 0x7e;
            });
        }

        [[nodiscard]] bool MatchesPersistentVersion(const EngineProductVersion &product,
                                                    const Application::EngineReleaseVersion &persistent) noexcept {
            const ReleaseSemanticVersion &release = product.value;
            const Application::HoroVersion &contract = persistent.value;
            return release.major == contract.major && release.minor == contract.minor && release.patch == contract.patch &&
                   release.prerelease == contract.prerelease;
        }

        [[nodiscard]] Result<ReleaseProductVersion> RequestedVersion(const std::span<const ReleaseVersionClaim> claims) {
            const auto requested = std::ranges::find(claims, ReleaseVersionClaimSource::Requested, &ReleaseVersionClaim::source);
            if (requested == claims.end() || std::holds_alternative<Application::EngineReleaseVersion>(requested->value))
                return Result<ReleaseProductVersion>::Failure(MakeError(ReleaseErrors::AuthorityInvalid));
            if (const auto *engine = std::get_if<EngineProductVersion>(&requested->value))
                return Result<ReleaseProductVersion>::Success(*engine);
            return Result<ReleaseProductVersion>::Success(std::get<GameProductVersion>(requested->value));
        }

        [[nodiscard]] Result<void> ValidateClaim(const ReleaseVersionClaim &claim, const ReleaseProductVersion &requested) {
            if (claim.source == ReleaseVersionClaimSource::PersistentContract) {
                const auto *engine = std::get_if<EngineProductVersion>(&requested);
                const auto *persistent = std::get_if<Application::EngineReleaseVersion>(&claim.value);
                if (!engine || !persistent)
                    return Result<void>::Failure(MakeError(ReleaseErrors::ProductKindMismatch));
                return MatchesPersistentVersion(*engine, *persistent)
                           ? Result<void>::Success()
                           : Result<void>::Failure(MakeError(ReleaseErrors::PersistentContractMismatch));
            }
            if (std::holds_alternative<Application::EngineReleaseVersion>(claim.value))
                return Result<void>::Failure(MakeError(ReleaseErrors::ProductKindMismatch));
            if (requested.index() != claim.value.index())
                return Result<void>::Failure(MakeError(ReleaseErrors::ProductKindMismatch));
            const bool matches = requested.index() == 0
                                     ? std::get<EngineProductVersion>(requested) == std::get<EngineProductVersion>(claim.value)
                                     : std::get<GameProductVersion>(requested) == std::get<GameProductVersion>(claim.value);
            return matches ? Result<void>::Success() : Result<void>::Failure(MakeError(ReleaseErrors::AuthorityConflict));
        }
    }  // namespace

    /** @copydoc ParseReleaseVersion */
    Result<ReleaseSemanticVersion> ParseReleaseVersion(const std::string_view text) {
        auto parts = SplitVersion(text);
        if (parts.HasError())
            return Result<ReleaseSemanticVersion>::Failure(MakeError(ReleaseErrors::VersionInvalid));
        auto core = ParseCore(parts.Value().core);
        if (core.HasError())
            return Result<ReleaseSemanticVersion>::Failure(MakeError(ReleaseErrors::VersionInvalid));
        return Result<ReleaseSemanticVersion>::Success(
            {core.Value()[0], core.Value()[1], core.Value()[2], std::string(parts.Value().prerelease), std::string(parts.Value().build)});
    }

    /** @copydoc ParseReleaseTag */
    Result<ReleaseSemanticVersion> ParseReleaseTag(const std::string_view text) {
        if (!text.starts_with('v'))
            return Result<ReleaseSemanticVersion>::Failure(MakeError(ReleaseErrors::TagInvalid));
        auto parsed = ParseReleaseVersion(text.substr(1));
        if (parsed.HasError())
            return Result<ReleaseSemanticVersion>::Failure(MakeError(ReleaseErrors::TagInvalid));
        return parsed;
    }

    /** @copydoc FormatReleaseVersion */
    std::string FormatReleaseVersion(const ReleaseSemanticVersion &version) {
        std::string formatted = std::format("{}.{}.{}", version.major, version.minor, version.patch);
        if (!version.prerelease.empty()) {
            formatted.push_back('-');
            formatted += version.prerelease;
        }
        if (!version.buildMetadata.empty()) {
            formatted.push_back('+');
            formatted += version.buildMetadata;
        }
        return formatted;
    }

    /** @copydoc CompareReleaseVersionPrecedence */
    std::strong_ordering CompareReleaseVersionPrecedence(const ReleaseSemanticVersion &lhs, const ReleaseSemanticVersion &rhs) noexcept {
        if (lhs.major != rhs.major)
            return lhs.major <=> rhs.major;
        if (lhs.minor != rhs.minor)
            return lhs.minor <=> rhs.minor;
        if (lhs.patch != rhs.patch)
            return lhs.patch <=> rhs.patch;
        return ComparePrerelease(lhs.prerelease, rhs.prerelease);
    }

    /** @copydoc ValidateReleaseVersionAuthority */
    Result<ReleaseVersionAuthority> ValidateReleaseVersionAuthority(const std::span<const ReleaseVersionClaim> claims,
                                                                    const ReleaseSourceRevision &sourceRevision) {
        if (claims.empty() || claims.size() > MaximumReleaseVersionClaims || !ValidSourceRevision(sourceRevision))
            return Result<ReleaseVersionAuthority>::Failure(MakeError(ReleaseErrors::AuthorityInvalid));
        std::array<bool, MaximumReleaseVersionClaims> seen{};
        for (const ReleaseVersionClaim &claim : claims) {
            if (!IsKnownSource(claim.source))
                return Result<ReleaseVersionAuthority>::Failure(MakeError(ReleaseErrors::AuthorityInvalid));
            const auto index = static_cast<std::size_t>(claim.source);
            if (seen[index])
                return Result<ReleaseVersionAuthority>::Failure(MakeError(ReleaseErrors::AuthorityConflict));
            seen[index] = true;
        }

        auto requested = RequestedVersion(claims);
        if (requested.HasError())
            return Result<ReleaseVersionAuthority>::Failure(requested.ErrorValue());
        for (const ReleaseVersionClaim &claim : claims) {
            if (const Result<void> valid = ValidateClaim(claim, requested.Value()); valid.HasError())
                return Result<ReleaseVersionAuthority>::Failure(valid.ErrorValue());
        }
        return Result<ReleaseVersionAuthority>::Success({std::move(requested).Value(), sourceRevision});
    }
}  // namespace Horo::Release
