#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/ProjectMigrationTransaction.h"

#include <algorithm>
#include <fstream>

namespace
{
using namespace Horo;
using namespace Horo::Application;
using namespace Horo::Editor;

const ErrorCodeDescriptor InjectedFailure{.domain = ErrorDomainId{"test.migration"},
                                          .code = ErrorCode{"injected"},
                                          .defaultSeverity = ErrorSeverity::Error,
                                          .summary = "Injected failure."};

class FailingFilesystem final : public DurableFileSystem
{
  public:
    explicit FailingFilesystem(std::filesystem::path root) : root_(std::move(root))
    {
    }

    Result<ExclusiveFileLock> TryAcquireExclusive(const std::filesystem::path &path, std::string_view owner) override
    {
        return native_.TryAcquireExclusive(path, owner);
    }

    Result<std::uint64_t> AvailableBytes(const std::filesystem::path &path) const override
    {
        return native_.AvailableBytes(path);
    }

    Result<void> WriteDurable(const std::filesystem::path &path, std::span<const std::byte> bytes) override
    {
        return native_.WriteDurable(path, bytes);
    }

    Result<void> CopyDurable(const std::filesystem::path &a, const std::filesystem::path &b) override
    {
        return native_.CopyDurable(a, b);
    }

    Result<void> AtomicReplace(const std::filesystem::path &a, const std::filesystem::path &b) override
    {
        if (failRoot && b == root_ / ".horo/project.json")
            return Result<void>::Failure(MakeError(InjectedFailure));
        return native_.AtomicReplace(a, b);
    }

    Result<void> RemoveDurable(const std::filesystem::path &path) override
    {
        return native_.RemoveDurable(path);
    }

    Result<void> SyncDirectory(const std::filesystem::path &path) override
    {
        return native_.SyncDirectory(path);
    }

    bool failRoot{true};

  private:
    std::filesystem::path root_;
    NativeDurableFileSystem native_;
};

std::vector<std::byte> Bytes(const std::string &text)
{
    std::vector<std::byte> result(text.size());
    std::transform(text.begin(), text.end(), result.begin(), [](char value) { return static_cast<std::byte>(value); });
    return result;
}

ContractBaselineVersion Version(const char *text)
{
    auto parsed = ParseHoroVersion(text);
    REQUIRE((parsed.HasValue()));
    return {parsed.Value()};
}

PersistentContractHash Contract(std::uint8_t marker)
{
    PersistentContractHash hash;
    hash.bytes.fill(marker);
    return hash;
}

class Append final : public IProjectMigrationDocumentStage
{
  public:
    MigrationStageDescriptor Describe() const override
    {
        return {.id = {"test.append"}, .writeFamilies = {"other"}};
    }

    Result<MigrationDocumentChange> Execute(const ProjectDocumentView &source, const MigrationStageContext &,
                                            const CancellationToken &) const override
    {
        auto output = std::vector<std::byte>(source.bytes.begin(), source.bytes.end());
        output.push_back(std::byte{'!'});
        return Result<MigrationDocumentChange>::Success({source.handle, std::move(output)});
    }
};

class Valid final : public IProjectMigrationValidator
{
  public:
    MigrationStageDescriptor Describe() const override
    {
        return {.id = {"test.valid"}};
    }

    Result<void> Validate(const ProjectMigrationContext &, const CancellationToken &) const override
    {
        return Result<void>::Success();
    }
};

class FinalCandidateValid final : public IProjectMigrationValidator
{
  public:
    MigrationStageDescriptor Describe() const override
    {
        return {.id = {"test.final_candidate"}};
    }

    Result<void> Validate(const ProjectMigrationContext &context, const CancellationToken &) const override
    {
        bool sawTargetRoot = false;
        bool sawHistory = false;
        for (const auto &entry : context.ListDocuments({}))
        {
            const auto document = context.ReadDocument(entry.handle);
            if (document.HasError())
                return Result<void>::Failure(document.ErrorValue());
            const auto text = std::string_view(reinterpret_cast<const char *>(document.Value().bytes.data()),
                                               document.Value().bytes.size());
            if (entry.path == ".horo/project.json")
                sawTargetRoot = text.find("\"horoVersion\": \"0.1.0\"") != std::string_view::npos &&
                                text.find("migrationHistoryHead") != std::string_view::npos;
            if (entry.path == ".horo/migration_history.json")
                sawHistory = text.find("test.migration") != std::string_view::npos;
        }
        return sawTargetRoot && sawHistory
                   ? Result<void>::Success()
                   : Result<void>::Failure(
                         MakeError(InjectedFailure, "Final validator did not see transaction documents."));
    }
};

struct Project
{
    Project()
    {
        root = std::filesystem::temp_directory_path() / "horo-transaction-test";
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::create_directories(root / ".horo/local");
        std::filesystem::create_directories(root / "assets");
        Write(
            ".horo/project.json",
            R"({"horoVersion":"0.0.1","persistentContract":"sha256:0101010101010101010101010101010101010101010101010101010101010101","projectId":"p1","name":"Test","projectVersion":"0.1.0","createdAt":"2026-07-18T00:00:00Z","settings":{"renderBackend":"opengl","unknown":7}})");
        Write("assets/data.txt", "value");
    }

    ~Project()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    void Write(const std::filesystem::path &path, const std::string &text)
    {
        std::filesystem::create_directories((root / path).parent_path());
        std::ofstream(root / path, std::ios::binary) << text;
    }

    std::string Read(const std::filesystem::path &path) const
    {
        std::ifstream stream(root / path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), {}};
    }

    std::filesystem::path root;
};

ProjectMigrationPlan Plan()
{
    auto builder = ProjectMigrationPipelineBuilder::Begin({"test.migration"});
    static_cast<void>(
        builder.AddForEach(MigrationDocumentQuery::Kind(MigrationDocumentKind::Other), std::make_shared<Append>()));
    static_cast<void>(builder.AddValidator(std::make_shared<Valid>()));
    auto pipeline = std::move(builder).Build();
    REQUIRE((pipeline.HasValue()));
    ProjectMigrationDefinition definition{.id = {"test.migration"},
                                          .from = Version("0.0.1"),
                                          .to = Version("0.1.0"),
                                          .sourceContract = Contract(1),
                                          .targetContract = Contract(2),
                                          .pipeline = pipeline.Value()};
    definition.storageEstimate = {.maximumOutputRatioPermille = 1000, .maximumAddedBytesPerDocument = 1};
    return {.source = Version("0.0.1"),
            .target = Version("0.1.0"),
            .definitions = {definition},
            .targetValidator = std::make_shared<FinalCandidateValid>()};
}

TEST_CASE("Transaction Publishes History And Preserves Unknown Metadata", "[unit][editor]")
{
    Project project;
    NativeDurableFileSystem files;
    SystemWallClock clock;
    ProjectMutationCoordinator coordinator(files);
    JobSystem jobs({.workerCount = 2, .maxQueuedJobs = 8});
    ProjectMigrationTransactionService service(files, clock, coordinator, jobs);
    ProjectMetadata metadata{.horoVersion = {ParseHoroVersion("0.0.1").Value()},
                             .persistentContract = Contract(1),
                             .projectId = "p1",
                             .name = "Test",
                             .projectVersion = "0.1.0",
                             .createdAt = "2026-07-18T00:00:00Z",
                             .renderBackend = "opengl"};
    ReleaseCompatibilityDecision target{.release = {ParseHoroVersion("0.1.0").Value()},
                                        .contractBaseline = Version("0.1.0"),
                                        .persistentContract = Contract(2)};
    auto result = service.Execute({.projectRoot = project.root,
                                   .sourceMetadata = metadata,
                                   .sourceBaseline = Version("0.0.1"),
                                   .targetDecision = target,
                                   .plan = Plan(),
                                   .engineBuildIdentity = "test-build"});
    REQUIRE((result.HasValue()));
    REQUIRE((project.Read("assets/data.txt") == "value!"));
    const auto root = project.Read(".horo/project.json");
    REQUIRE((root.find("\"horoVersion\": \"0.1.0\"") != std::string::npos));
    REQUIRE((root.find("\"unknown\": 7") != std::string::npos));
    REQUIRE((root.find("migrationHistoryHead") != std::string::npos));
    REQUIRE((project.Read(".horo/migration_history.json").find(result.Value().operationId) != std::string::npos));
    REQUIRE((project.Read(".horo/migration_history.json").find("\n  ") == std::string::npos));
    REQUIRE((service.InspectPendingRecovery(project.root).action == MigrationRecoveryAction::None));
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Recovery Resumes After History Before Root Failure", "[unit][editor]")
{
    Project project;
    FailingFilesystem files(project.root);
    SystemWallClock clock;
    ProjectMutationCoordinator coordinator(files);
    JobSystem jobs({.workerCount = 2, .maxQueuedJobs = 8});
    ProjectMigrationTransactionService service(files, clock, coordinator, jobs);
    ProjectMetadata metadata{.horoVersion = {ParseHoroVersion("0.0.1").Value()},
                             .persistentContract = Contract(1),
                             .projectId = "p1",
                             .name = "Test",
                             .projectVersion = "0.1.0",
                             .createdAt = "2026-07-18T00:00:00Z",
                             .renderBackend = "opengl"};
    ReleaseCompatibilityDecision target{.release = {ParseHoroVersion("0.1.0").Value()},
                                        .contractBaseline = Version("0.1.0"),
                                        .persistentContract = Contract(2)};
    auto failed = service.Execute({.projectRoot = project.root,
                                   .sourceMetadata = metadata,
                                   .sourceBaseline = Version("0.0.1"),
                                   .targetDecision = target,
                                   .plan = Plan(),
                                   .engineBuildIdentity = "test-build"});
    REQUIRE((failed.HasError()));
    REQUIRE((project.Read(".horo/project.json").find("\"horoVersion\":\"0.0.1\"") != std::string::npos));
    REQUIRE((project.Read(".horo/migration_history.json").find("test.migration") != std::string::npos));
    const auto recovery = service.InspectPendingRecovery(project.root);
    REQUIRE((recovery.action == MigrationRecoveryAction::ResumePublish));
    files.failRoot = false;
    REQUIRE((service.Recover(project.root).HasValue()));
    REQUIRE((project.Read(".horo/project.json").find("\"horoVersion\": \"0.1.0\"") != std::string::npos));
    REQUIRE((service.InspectPendingRecovery(project.root).action == MigrationRecoveryAction::None));
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Recovery Restores Verified Originals When Forward Evidence Is Lost", "[unit][editor]")
{
    Project project;
    FailingFilesystem files(project.root);
    SystemWallClock clock;
    ProjectMutationCoordinator coordinator(files);
    JobSystem jobs({.workerCount = 2, .maxQueuedJobs = 8});
    ProjectMigrationTransactionService service(files, clock, coordinator, jobs);
    ProjectMetadata metadata{.horoVersion = {ParseHoroVersion("0.0.1").Value()},
                             .persistentContract = Contract(1),
                             .projectId = "p1",
                             .name = "Test",
                             .projectVersion = "0.1.0",
                             .createdAt = "2026-07-18T00:00:00Z",
                             .renderBackend = "opengl"};
    ReleaseCompatibilityDecision target{.release = {ParseHoroVersion("0.1.0").Value()},
                                        .contractBaseline = Version("0.1.0"),
                                        .persistentContract = Contract(2)};
    auto failed = service.Execute({.projectRoot = project.root,
                                   .sourceMetadata = metadata,
                                   .sourceBaseline = Version("0.0.1"),
                                   .targetDecision = target,
                                   .plan = Plan(),
                                   .engineBuildIdentity = "test-build"});
    REQUIRE((failed.HasError()));
    const auto recovery = service.InspectPendingRecovery(project.root);
    REQUIRE((recovery.operationId.has_value()));
    const auto operationRoot = project.root / ".horo/local/migration" / recovery.operationId.value();
    std::error_code ignored;
    std::filesystem::remove(operationRoot / "staging/.horo/project.json", ignored);
    REQUIRE((service.InspectPendingRecovery(project.root).action == MigrationRecoveryAction::RestoreOriginals));
    files.failRoot = false;
    REQUIRE((service.Recover(project.root).HasValue()));
    REQUIRE((project.Read("assets/data.txt") == "value"));
    REQUIRE((project.Read(".horo/project.json").find("\"horoVersion\":\"0.0.1\"") != std::string::npos));
    REQUIRE((!std::filesystem::exists(project.root / ".horo/migration_history.json")));
    REQUIRE((service.InspectPendingRecovery(project.root).action == MigrationRecoveryAction::None));
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Recovery Journal Rejects Duplicate Keys", "[unit][editor]")
{
    Project project;
    const std::string operationId(32, 'a');
    const std::string writer = FormatHoroVersion(CurrentEngineReleaseVersion().value);
    project.Write(
        ".horo/local/migration/" + operationId + "/journal.json",
        "{\"writerHoroVersion\":\"" + writer + "\",\"operationId\":\"" + operationId +
            "\",\"state\":\"Preparing\",\"state\":\"Committed\",\"sourceVersion\":\"0.0.1\"," +
            "\"targetVersion\":\"0.1.0\",\"projectId\":\"p1\",\"engineBuildIdentity\":\"test\",\"records\":[]}");
    NativeDurableFileSystem files;
    SystemWallClock clock;
    ProjectMutationCoordinator coordinator(files);
    JobSystem jobs({.workerCount = 1, .maxQueuedJobs = 1});
    ProjectMigrationTransactionService service(files, clock, coordinator, jobs);
    REQUIRE((service.InspectPendingRecovery(project.root).action == MigrationRecoveryAction::Unrecoverable));
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Supported Old Writer And Committed Cleanup Are Handled Separately", "[unit][editor]")
{
    Project project;
    const std::string operationId(32, 'b');
    const std::string contract = FormatMigrationRecoveryContractId(CurrentMigrationRecoveryContractId());
    const std::string journal = "{\"writerHoroVersion\":\"0.0.0\",\"recoveryContract\":\"" + contract +
                                "\",\"operationId\":\"" + operationId +
                                "\",\"state\":\"Preparing\",\"sourceVersion\":\"0.0.0\","
                                "\"targetVersion\":\"0.0.1\",\"projectId\":\"p1\","
                                "\"engineBuildIdentity\":\"old-test\",\"records\":[]}";
    project.Write(".horo/local/migration/" + operationId + "/journal.json", journal);
    NativeDurableFileSystem files;
    SystemWallClock clock;
    ProjectMutationCoordinator coordinator(files);
    JobSystem jobs({.workerCount = 1, .maxQueuedJobs = 1});
    ProjectMigrationTransactionService service(files, clock, coordinator, jobs);
    REQUIRE(
        (service.InspectPendingRecovery(project.root).action == MigrationRecoveryAction::DiscardUnpublishedStaging));
    REQUIRE((service.Recover(project.root).HasValue()));

    std::string committed = journal;
    const auto statePosition = committed.find("Preparing");
    committed.replace(statePosition, std::string("Preparing").size(), "Committed");
    project.Write(".horo/local/migration-cleanup/" + operationId + "/journal.json", committed);
    REQUIRE((service.InspectPendingRecovery(project.root).action == MigrationRecoveryAction::None));
    REQUIRE((service.CleanupCommittedMigrations(project.root).HasValue()));
    REQUIRE((!std::filesystem::exists(project.root / ".horo/local/migration-cleanup" / operationId)));
    jobs.Shutdown(ShutdownPolicy::Drain);
}
} // namespace
