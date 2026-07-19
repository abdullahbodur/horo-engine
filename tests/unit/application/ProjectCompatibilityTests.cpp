#include <catch2/catch_test_macros.hpp>

#include "Horo/Application/ProjectCompatibility.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    using namespace Horo;
    using namespace Horo::Application;

    class TemporaryProject
    {
    public:
        TemporaryProject()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root_ = std::filesystem::temp_directory_path() / ("horo-compatibility-" + std::to_string(stamp));
            std::filesystem::create_directories(root_ / ".horo");
        }

        ~TemporaryProject()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        void Write(const std::string& text) const
        {
            std::ofstream output(root_ / ".horo/project.json", std::ios::binary);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

        [[nodiscard]] const std::filesystem::path& Root() const noexcept
        {
            return root_;
        }

    private:
        std::filesystem::path root_;
    };

    class AcceptingVerifier final : public ICompatibilityProofVerifier
    {
    public:
        Result<void> Verify(const CompatibilityProof&, const PersistentContractHash&) const override
        {
            return Result<void>::Success();
        }
    };

    PersistentContractHash Hash(const char digit)
    {
        const auto parsed = ParsePersistentContractHash("sha256:" + std::string(64, digit));
        REQUIRE((parsed.HasValue()));
        return parsed.Value();
    }

    CompatibilityDecisionHash DecisionHash(const char digit)
    {
        const auto parsed = ParseCompatibilityDecisionHash("sha256:" + std::string(64, digit));
        REQUIRE((parsed.HasValue()));
        return parsed.Value();
    }

    HoroVersion Version(const std::string_view text)
    {
        const auto parsed = ParseHoroVersion(text);
        REQUIRE((parsed.HasValue()));
        return parsed.Value();
    }

    std::string Metadata(const std::string_view version, const PersistentContractHash& contract,
                         const std::string_view proof = {})
    {
        return "{\n"
            "  \"horoVersion\": \"" +
            std::string(version) +
            "\",\n"
            "  \"persistentContract\": \"" +
            FormatPersistentContractHash(contract) +
            "\",\n"
            "  \"projectId\": \"project-id\",\n"
            "  \"name\": \"Horo Project\",\n"
            "  \"projectVersion\": \"0.4.0\",\n"
            "  \"createdAt\": \"2026-07-18T00:00:00Z\",\n"
            "  \"settings\": { \"renderBackend\": \"metal\" }" +
            (proof.empty() ? std::string{} : ",\n  \"compatibilityProof\": " + std::string(proof)) + "\n}\n";
    }

    ReleaseCompatibilityRegistry Registry(const PersistentContractHash& contract)
    {
        const std::array decisions{
            ReleaseCompatibilityDecision{
                {Version("1.0.3")},
                {Version("1.0.3")},
                contract,
                DecisionHash('1'),
                CompatibilityDecisionKind::EstablishBaseline
            },
            ReleaseCompatibilityDecision{
                {Version("1.0.5")},
                {Version("1.0.3")},
                contract,
                DecisionHash('2'),
                CompatibilityDecisionKind::CompatibleReleaseLine
            }
        };
        auto registry = ReleaseCompatibilityRegistry::Create(decisions);
        REQUIRE((registry.HasValue()));
        return std::move(registry).Value();
    }

    TEST_CASE("Sem Ver Is Canonical And Ordered", "[unit][application]")
    {
        const std::array valid{"0.0.1", "1.2.3-alpha", "1.2.3-alpha.1", "1.2.3-rc-1"};
        for (const auto text : valid)
        {
            const auto parsed = ParseHoroVersion(text);
            REQUIRE((parsed.HasValue()));
            REQUIRE((FormatHoroVersion(parsed.Value()) == text));
        }
        for (const auto invalid :
             {
                 "", "1.0", "01.0.0", "1.00.0", "1.0.00", "1.0.0-01", "1.0.0+build", "1.0.0-", "1.0.0-a..b",
                 "4294967296.0.0"
             })
            REQUIRE((ParseHoroVersion(invalid).HasError()));
        REQUIRE((CompareHoroVersions(Version("1.0.0-alpha"), Version("1.0.0-alpha.1")) < 0));
        REQUIRE((CompareHoroVersions(Version("1.0.0-rc.1"), Version("1.0.0")) < 0));
        REQUIRE((ParsePersistentContractHash("sha256:" + std::string(64, 'A')).HasError()));
        REQUIRE((FormatPersistentContractHash(Hash('a')) == "sha256:" + std::string(64, 'a')));
    }

    TEST_CASE("Registry Rejects Ambiguity And Patch Drift", "[unit][application]")
    {
        const auto contract = Hash('a');
        const ReleaseCompatibilityDecision first{
            {Version("1.0.0")},
            {Version("1.0.0")},
            contract,
            DecisionHash('1'),
            CompatibilityDecisionKind::EstablishBaseline
        };
        const std::array duplicate{first, first};
        REQUIRE((ReleaseCompatibilityRegistry::Create(duplicate).HasError()));

        const std::array drift{
            first, ReleaseCompatibilityDecision{
                {Version("1.0.1")},
                {Version("1.0.0")},
                Hash('b'),
                DecisionHash('2'),
                CompatibilityDecisionKind::CompatibleReleaseLine
            }
        };
        REQUIRE((ReleaseCompatibilityRegistry::Create(drift).HasError()));

        const std::array missingBaseline{
            ReleaseCompatibilityDecision{
                {Version("1.1.0")},
                {Version("1.0.0")},
                contract,
                DecisionHash('3'),
                CompatibilityDecisionKind::CompatibleReleaseLine
            }
        };
        REQUIRE((ReleaseCompatibilityRegistry::Create(missingBaseline).HasError()));
    }

    TEST_CASE("Inspector Classifies Known And Proven Future Patches", "[unit][application]")
    {
        const auto contract = Hash('a');
        const auto registry = Registry(contract);
        const EngineReleaseVersion current{Version("1.0.5")};
        RejectingCompatibilityProofVerifier rejecting;
        ProjectCompatibilityInspector inspector{registry, current, rejecting};
        TemporaryProject project;

        project.Write(Metadata("1.0.5", contract));
        REQUIRE((inspector.Inspect(project.Root()).status == ProjectCompatibilityStatus::Current));
        project.Write(Metadata("1.0.3", contract));
        const auto olderPatch = inspector.Inspect(project.Root());
        REQUIRE((olderPatch.status == ProjectCompatibilityStatus::CompatibleReleaseLine));
        REQUIRE((olderPatch.markerUpdateRequired));

        const std::string proof = "{\"release\":\"1.0.7\",\"contractBaseline\":\"1.0.3\","
            "\"decisionHash\":\"sha256:" +
            std::string(64, '3') + "\",\"signature\":\"fixture\"}";
        project.Write(Metadata("1.0.7", contract, proof));
        REQUIRE((inspector.Inspect(project.Root()).status == ProjectCompatibilityStatus::FutureVersion));

        AcceptingVerifier accepting;
        ProjectCompatibilityInspector trustedInspector{registry, current, accepting};
        REQUIRE((trustedInspector.Inspect(project.Root()).status == ProjectCompatibilityStatus::CompatibleReleaseLine));

        project.Write(Metadata("1.0.7", contract));
        REQUIRE((trustedInspector.Inspect(project.Root()).status == ProjectCompatibilityStatus::FutureVersion));

        project.Write(Metadata("1.1.0", contract));
        REQUIRE((trustedInspector.Inspect(project.Root()).status == ProjectCompatibilityStatus::FutureVersion));
        project.Write(Metadata("1.0.5", Hash('b')));
        REQUIRE((trustedInspector.Inspect(project.Root()).status == ProjectCompatibilityStatus::FutureVersion));
    }

    TEST_CASE("Bounded Metadata Rejects Legacy And Malformed Inputs", "[unit][application]")
    {
        TemporaryProject project;
        project.Write(R"({"formatVersion":1,"projectId":"old"})");
        REQUIRE((LoadProjectMetadata(project.Root()).HasError()));
        project.Write(R"({"horoVersion":"0.0.1","horoVersion":"0.0.1"})");
        REQUIRE((LoadProjectMetadata(project.Root()).HasError()));
        project.Write(std::string(64U * 1024U + 1U, 'x'));
        REQUIRE((LoadProjectMetadata(project.Root()).HasError()));

        project.Write(Metadata("0.0.1", Hash('a')));
        std::string invalidRenderer = Metadata("0.0.1", Hash('a'));
        invalidRenderer.replace(invalidRenderer.find("metal"), 5, "Metal");
        project.Write(invalidRenderer);
        REQUIRE((LoadProjectMetadata(project.Root()).HasError()));

        std::string unicode = Metadata("0.0.1", Hash('a'));
        unicode.replace(unicode.find("Horo Project"), 12, "Horo \\u0130stanbul");
        project.Write(unicode);
        const auto loadedUnicode = LoadProjectMetadata(project.Root());
        REQUIRE((loadedUnicode.HasValue()));
        REQUIRE((!loadedUnicode.Value().name.empty()));

        std::string nested = Metadata("0.0.1", Hash('a'));
        nested.insert(nested.rfind('}'), ",\"nested\":" + std::string(17, '[') + "0" + std::string(17, ']'));
        project.Write(nested);
        REQUIRE((LoadProjectMetadata(project.Root()).HasError()));

        TemporaryProject missing;
        std::filesystem::remove(missing.Root() / ".horo/project.json");
        const auto registry = Registry(Hash('a'));
        RejectingCompatibilityProofVerifier verifier;
        const ProjectCompatibilityInspector inspector{registry, {Version("1.0.5")}, verifier};
        REQUIRE((inspector.Inspect(missing.Root()).status == ProjectCompatibilityStatus::Inaccessible));
    }
} // namespace
