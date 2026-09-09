#include "Horo/Release/ReleaseErrors.h"
#include "Horo/Release/ReleaseVersion.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <string>
#include <type_traits>
#include <variant>

namespace {
    using namespace Horo;
    using namespace Horo::Release;

    ReleaseSemanticVersion Version(const std::string_view text) {
        auto parsed = ParseReleaseVersion(text);
        REQUIRE((parsed.HasValue()));
        return std::move(parsed).Value();
    }

    EngineProductVersion Engine(const std::string_view text) {
        return {Version(text)};
    }

    GameProductVersion Game(const std::string_view text) {
        return {Version(text)};
    }

    Application::EngineReleaseVersion Persistent(const std::string_view text) {
        auto parsed = Application::ParseHoroVersion(text);
        REQUIRE((parsed.HasValue()));
        return {std::move(parsed).Value()};
    }

    ReleaseVersionClaim EngineClaim(const ReleaseVersionClaimSource source, const std::string_view text) {
        return {source, Engine(text)};
    }

    ReleaseVersionClaim GameClaim(const ReleaseVersionClaimSource source, const std::string_view text) {
        return {source, Game(text)};
    }

    void RequireError(const auto &result, const std::string_view code) {
        REQUIRE((result.HasError()));
        REQUIRE((result.ErrorValue().code.Value() == code));
    }

    TEST_CASE("Release versions preserve identity and SemVer precedence", "[unit][application][release][headless]") {
        for (const std::string_view text : {"0.0.0", "1.2.3", "1.2.3-alpha.1", "1.2.3+build.7", "1.2.3-rc.1+sha.abc"}) {
            const ReleaseSemanticVersion parsed = Version(text);
            REQUIRE((FormatReleaseVersion(parsed) == text));
            REQUIRE((parsed.IsStable() == (parsed.prerelease.empty())));
        }

        const ReleaseSemanticVersion firstBuild = Version("1.2.3+first");
        const ReleaseSemanticVersion secondBuild = Version("1.2.3+second");
        REQUIRE((firstBuild != secondBuild));
        REQUIRE((CompareReleaseVersionPrecedence(firstBuild, secondBuild) == std::strong_ordering::equal));

        const std::array precedence{"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                    "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
        for (std::size_t index = 1; index < precedence.size(); ++index)
            REQUIRE((CompareReleaseVersionPrecedence(Version(precedence[index - 1]), Version(precedence[index])) ==
                     std::strong_ordering::less));
    }

    TEST_CASE("Release version parsing rejects ambiguous and unbounded text", "[unit][application][release][headless]") {
        const std::array invalid{"",       "1",      "1.2",           "1.2.3.4",        "01.2.3",         "1.02.3",        "1.2.03",
                                 "1.2.3-", "1.2.3+", "1.2.3-a..b",    "1.2.3+a..b",     "1.2.3-01",       "1.2.3+a+b",     "v1.2.3",
                                 " 1.2.3", "1.2.3 ", "1.2.3-alpha_1", "4294967296.0.0", "0.4294967296.0", "0.0.4294967296"};
        for (const std::string_view text : invalid)
            RequireError(ParseReleaseVersion(text), "release.version.invalid");

        std::string nonAscii{"1.2.3-"};
        nonAscii.append("\xC3\xA9", 2);
        RequireError(ParseReleaseVersion(nonAscii), "release.version.invalid");

        const std::string exactLimit = "1.2.3-" + std::string(MaximumReleaseVersionBytes - 6, 'a');
        REQUIRE((exactLimit.size() == MaximumReleaseVersionBytes));
        REQUIRE((ParseReleaseVersion(exactLimit).HasValue()));
        RequireError(ParseReleaseVersion(exactLimit + "a"), "release.version.invalid");
        REQUIRE((ParseReleaseVersion("1.2.3+001").HasValue()));
    }

    TEST_CASE("Release tags require one lowercase v prefix", "[unit][application][release][headless]") {
        REQUIRE((FormatReleaseVersion(ParseReleaseTag("v2.4.0-rc.1+sha.7").Value()) == "2.4.0-rc.1+sha.7"));
        for (const std::string_view invalid : {"2.4.0", "V2.4.0", "vv2.4.0", "v2.4", "v"})
            RequireError(ParseReleaseTag(invalid), "release.version.tag_invalid");
    }

    TEST_CASE("Engine release authority accepts exact claims and compatibility projection", "[unit][application][release][headless]") {
        static_assert(!std::is_convertible_v<EngineProductVersion, GameProductVersion>);
        static_assert(!std::is_convertible_v<GameProductVersion, EngineProductVersion>);

        const std::array claims{EngineClaim(ReleaseVersionClaimSource::Requested, "3.1.0-rc.2+sha.abc"),
                                EngineClaim(ReleaseVersionClaimSource::Tag, "3.1.0-rc.2+sha.abc"),
                                EngineClaim(ReleaseVersionClaimSource::Manifest, "3.1.0-rc.2+sha.abc"),
                                EngineClaim(ReleaseVersionClaimSource::Notes, "3.1.0-rc.2+sha.abc"),
                                ReleaseVersionClaim{ReleaseVersionClaimSource::PersistentContract, Persistent("3.1.0-rc.2")}};
        const auto authority = ValidateReleaseVersionAuthority(claims, {"revision-abc123"});
        REQUIRE((authority.HasValue()));
        REQUIRE((std::get<EngineProductVersion>(authority.Value().productVersion) == Engine("3.1.0-rc.2+sha.abc")));
        REQUIRE((authority.Value().sourceRevision.value == "revision-abc123"));
    }

    TEST_CASE("Game release authority remains independent from engine identity", "[unit][application][release][headless]") {
        const std::array claims{GameClaim(ReleaseVersionClaimSource::Requested, "5.0.0"),
                                GameClaim(ReleaseVersionClaimSource::Tag, "5.0.0"), GameClaim(ReleaseVersionClaimSource::Manifest, "5.0.0"),
                                GameClaim(ReleaseVersionClaimSource::Notes, "5.0.0")};
        const auto authority = ValidateReleaseVersionAuthority(claims, {"game-revision"});
        REQUIRE((authority.HasValue()));
        REQUIRE((std::get<GameProductVersion>(authority.Value().productVersion) == Game("5.0.0")));
    }

    TEST_CASE("Release authority rejects identity and product-kind conflicts", "[unit][application][release][headless]") {
        for (const ReleaseVersionClaimSource source :
             {ReleaseVersionClaimSource::Tag, ReleaseVersionClaimSource::Manifest, ReleaseVersionClaimSource::Notes}) {
            const std::array claims{EngineClaim(ReleaseVersionClaimSource::Requested, "1.2.3+one"), EngineClaim(source, "1.2.3+two")};
            RequireError(ValidateReleaseVersionAuthority(claims, {"revision"}), "release.version.authority_conflict");
        }

        const std::array mixed{EngineClaim(ReleaseVersionClaimSource::Requested, "1.2.3"),
                               GameClaim(ReleaseVersionClaimSource::Manifest, "1.2.3")};
        RequireError(ValidateReleaseVersionAuthority(mixed, {"revision"}), "release.version.product_kind_mismatch");

        const std::array enginePersistentMismatch{EngineClaim(ReleaseVersionClaimSource::Requested, "1.2.3+build"),
                                                  ReleaseVersionClaim{ReleaseVersionClaimSource::PersistentContract, Persistent("1.2.4")}};
        RequireError(ValidateReleaseVersionAuthority(enginePersistentMismatch, {"revision"}),
                     "release.version.persistent_contract_mismatch");

        const std::array enginePrereleaseMismatch{EngineClaim(ReleaseVersionClaimSource::Requested, "1.2.3-rc.2+build"),
                                                  ReleaseVersionClaim{ReleaseVersionClaimSource::PersistentContract,
                                                                      Persistent("1.2.3-rc.1")}};
        RequireError(ValidateReleaseVersionAuthority(enginePrereleaseMismatch, {"revision"}),
                     "release.version.persistent_contract_mismatch");

        const std::array gamePersistent{GameClaim(ReleaseVersionClaimSource::Requested, "1.2.3"),
                                        ReleaseVersionClaim{ReleaseVersionClaimSource::PersistentContract, Persistent("1.2.3")}};
        RequireError(ValidateReleaseVersionAuthority(gamePersistent, {"revision"}), "release.version.product_kind_mismatch");
    }

    TEST_CASE("Release authority rejects malformed claim sets and revisions", "[unit][application][release][headless]") {
        RequireError(ValidateReleaseVersionAuthority({}, {"revision"}), "release.version.authority_invalid");

        const std::array missingRequested{EngineClaim(ReleaseVersionClaimSource::Tag, "1.0.0")};
        RequireError(ValidateReleaseVersionAuthority(missingRequested, {"revision"}), "release.version.authority_invalid");

        const std::array duplicate{EngineClaim(ReleaseVersionClaimSource::Requested, "1.0.0"),
                                   EngineClaim(ReleaseVersionClaimSource::Requested, "1.0.0")};
        RequireError(ValidateReleaseVersionAuthority(duplicate, {"revision"}), "release.version.authority_conflict");

        const std::array unknown{ReleaseVersionClaim{static_cast<ReleaseVersionClaimSource>(255), Engine("1.0.0")}};
        RequireError(ValidateReleaseVersionAuthority(unknown, {"revision"}), "release.version.authority_invalid");

        const std::array valid{EngineClaim(ReleaseVersionClaimSource::Requested, "1.0.0")};
        RequireError(ValidateReleaseVersionAuthority(valid, {}), "release.version.authority_invalid");
        RequireError(ValidateReleaseVersionAuthority(valid, {"revision with spaces"}), "release.version.authority_invalid");
        RequireError(ValidateReleaseVersionAuthority(valid, {std::string(MaximumReleaseSourceRevisionBytes + 1, 'a')}),
                     "release.version.authority_invalid");
        REQUIRE((ValidateReleaseVersionAuthority(valid, {std::string(MaximumReleaseSourceRevisionBytes, 'a')}).HasValue()));
    }

    TEST_CASE("Release error identities are distinct and stable", "[unit][application][release][headless]") {
        const std::array codes{ReleaseErrors::VersionInvalid.code.Value(),      ReleaseErrors::TagInvalid.code.Value(),
                               ReleaseErrors::AuthorityInvalid.code.Value(),    ReleaseErrors::AuthorityConflict.code.Value(),
                               ReleaseErrors::ProductKindMismatch.code.Value(), ReleaseErrors::PersistentContractMismatch.code.Value()};
        for (std::size_t first = 0; first < codes.size(); ++first) {
            for (std::size_t second = first + 1; second < codes.size(); ++second)
                REQUIRE((codes[first] != codes[second]));
        }
    }
}  // namespace
