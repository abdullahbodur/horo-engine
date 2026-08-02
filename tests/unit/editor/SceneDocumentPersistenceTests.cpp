#include "editor/document/SceneDocumentComparison.h"
#include "editor/document/SceneDocumentPersistence.h"
#include "editor/document/SceneFileWatchService.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>

namespace {
    using namespace Horo;
    using namespace Horo::Editor;

    const ErrorCodeDescriptor InjectedReplaceFailure{
        .domain = ErrorDomainId{"test.scene_persistence"},
        .code = ErrorCode{"replace_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Injected scene replacement failure.",
    };

    class TemporaryProject final {
    public:
        TemporaryProject() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root_ = std::filesystem::temp_directory_path() / ("horo-scene-persistence-" + std::to_string(stamp));
            std::filesystem::create_directories(root_ / ".horo");
            std::filesystem::create_directories(root_ / "assets/scenes");
        }

        ~TemporaryProject() {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        [[nodiscard]] const std::filesystem::path &Root() const noexcept {
            return root_;
        }

        [[nodiscard]] std::filesystem::path ScenePath() const {
            return root_ / "assets/scenes/main.horo";
        }

        [[nodiscard]] std::filesystem::path RecoveryPath() const {
            return root_ / ".horo/local/recovery/default-scene.hororecovery";
        }

        void WriteMetadata(const std::string &defaultScene = "assets/scenes/main.horo") const {
            std::ofstream output(root_ / ".horo/project.json", std::ios::binary);
            output << R"({"settings":{"defaultScene":")" << defaultScene << R"("}})";
        }

        void WriteScene(std::string contents) const {
            std::ofstream output(ScenePath(), std::ios::binary);
            output << contents;
        }

    private:
        std::filesystem::path root_;
    };

    class ReplaceFailingFileSystem final : public DurableFileSystem {
    public:
        [[nodiscard]] Result<ExclusiveFileLock> TryAcquireExclusive(const std::filesystem::path &path,
                                                                    const std::string_view ownerMetadata) override {
            return native_.TryAcquireExclusive(path, ownerMetadata);
        }

        [[nodiscard]] Result<std::uint64_t> AvailableBytes(const std::filesystem::path &path) const override {
            return native_.AvailableBytes(path);
        }

        [[nodiscard]] Result<void> WriteDurable(const std::filesystem::path &path, const std::span<const std::byte> bytes) override {
            return native_.WriteDurable(path, bytes);
        }

        [[nodiscard]] Result<void> CopyDurable(const std::filesystem::path &source, const std::filesystem::path &destination) override {
            return native_.CopyDurable(source, destination);
        }

        [[nodiscard]] Result<void> AtomicReplace(const std::filesystem::path &, const std::filesystem::path &) override {
            return Result<void>::Failure(MakeError(InjectedReplaceFailure));
        }

        [[nodiscard]] Result<void> RemoveDurable(const std::filesystem::path &path) override {
            return native_.RemoveDurable(path);
        }

        [[nodiscard]] Result<void> SyncDirectory(const std::filesystem::path &path) override {
            return native_.SyncDirectory(path);
        }

    private:
        NativeDurableFileSystem native_;
    };

    class InterferingFileSystem final : public DurableFileSystem {
    public:
        explicit InterferingFileSystem(std::filesystem::path canonicalPath) : canonicalPath_(std::move(canonicalPath)) {}

        [[nodiscard]] Result<ExclusiveFileLock> TryAcquireExclusive(const std::filesystem::path &path,
                                                                    const std::string_view ownerMetadata) override {
            return native_.TryAcquireExclusive(path, ownerMetadata);
        }

        [[nodiscard]] Result<std::uint64_t> AvailableBytes(const std::filesystem::path &path) const override {
            return native_.AvailableBytes(path);
        }

        [[nodiscard]] Result<void> WriteDurable(const std::filesystem::path &path, const std::span<const std::byte> bytes) override {
            Result<void> written = native_.WriteDurable(path, bytes);
            if (written.HasValue() && path.extension() == ".tmp") {
                std::ofstream external(canonicalPath_, std::ios::binary | std::ios::trunc);
                external << "{\n  \"schemaVersion\": 1,\n  \"objects\": []\n}\n";
            }
            return written;
        }

        [[nodiscard]] Result<void> CopyDurable(const std::filesystem::path &source, const std::filesystem::path &destination) override {
            return native_.CopyDurable(source, destination);
        }

        [[nodiscard]] Result<void> AtomicReplace(const std::filesystem::path &prepared, const std::filesystem::path &destination) override {
            return native_.AtomicReplace(prepared, destination);
        }

        [[nodiscard]] Result<void> RemoveDurable(const std::filesystem::path &path) override {
            return native_.RemoveDurable(path);
        }

        [[nodiscard]] Result<void> SyncDirectory(const std::filesystem::path &path) override {
            return native_.SyncDirectory(path);
        }

    private:
        std::filesystem::path canonicalPath_;
        NativeDurableFileSystem native_;
    };

    [[nodiscard]] SceneDocumentSnapshot AuthoredScene() {
        SceneDocument document;
        EditorHistory history;
        SceneDocumentCommandExecutor commands(document, history);
        const Math::Transform transform{
            .translation = {2.0F, 3.0F, -4.0F},
            .rotation = Math::Quaternion::FromEulerRadians({0.1F, 0.2F, 0.3F}),
            .scale = {1.5F, 2.0F, 0.5F},
        };
        const auto created = commands
                                 .Execute(
                                     CreateSceneObjectCommand{
                                         .name = "Persisted Box",
                                         .localTransform = transform,
                                         .primitiveMesh = PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box),
                                         .components =
                                             SceneObjectComponentSet{
                                                 .camera = Runtime::CameraComponent{.nearPlane = 0.25F, .farPlane = 500.0F},
                                                 .light = Runtime::LightComponent{.kind = Runtime::LightKind::Point, .intensity = 3.0F},
                                                 .triggerVolume = Runtime::TriggerVolumeComponent{Runtime::ColliderShapeType::Sphere},
                                                 .audioSource = Runtime::AudioSourceComponent{.gain = 0.75F, .spatial = false},
                                                 .behaviors =
                                                     {
                                                         Gameplay::BehaviorComponent{
                                                             .instanceId = Gameplay::BehaviorInstanceId{44},
                                                             .typeId =
                                                                 Gameplay::BehaviorTypeId::Parse("game.tests.persisted_behavior").Value(),
                                                             .schemaVersion = 3,
                                                             .enabled = false,
                                                             .fields =
                                                                 {
                                                                     Gameplay::BehaviorField{"speed", 2.5},
                                                                     Gameplay::
                                                                         BehaviorField{"label", std::string{"Unknown payload survives"}},
                                                                     Gameplay::BehaviorField{"offset", Math::Vec3{1.0F, 2.0F, 3.0F}},
                                                                 },
                                                         },
                                                     },
                                             },
                                     });
        REQUIRE((created.HasValue()));
        return document.Snapshot();
    }

    void RequireSameSceneObject(const SceneObjectSnapshot &actual, const SceneObjectSnapshot &expected) {
        REQUIRE((actual.id == expected.id));
        REQUIRE((actual.name == expected.name));
        REQUIRE((actual.parent == expected.parent));
        REQUIRE((actual.localTransform == expected.localTransform));
        REQUIRE((actual.primitiveMesh == expected.primitiveMesh));
        REQUIRE((actual.components == expected.components));
    }
}  // namespace

TEST_CASE("Project Scene Save Reopens The Same Authored State", "[unit][editor][persistence]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");

    NativeDurableFileSystem files;
    ProjectMutationCoordinator mutations(files);
    const SceneDocumentSnapshot authored = AuthoredScene();
    auto expected = InspectProjectSceneFingerprint(project.Root(), project.ScenePath());
    REQUIRE((expected.HasValue()));
    auto saved = SaveProjectScene(project.Root(), project.ScenePath(), authored, expected.Value(), false, mutations, files);
    REQUIRE((saved.HasValue()));
    REQUIRE((saved.Value().status == ProjectSceneSaveStatus::Saved));

    auto loaded = LoadProjectDefaultScene(project.Root());
    const std::string loadError = loaded.HasError() ? loaded.ErrorValue().code.Value() + ": " + loaded.ErrorValue().message : std::string{};
    INFO(loadError);
    REQUIRE((loaded.HasValue()));
    REQUIRE((loaded.Value().has_value()));
    REQUIRE((loaded.Value()->absolutePath.is_absolute()));
    REQUIRE((loaded.Value()->objects.size() == 1));

    SceneDocument reopened;
    REQUIRE((reopened.LoadSaved(std::move(loaded.Value()->objects)).HasValue()));
    REQUIRE((!reopened.IsDirty()));
    REQUIRE((reopened.Objects().size() == 1));
    REQUIRE((reopened.Objects().front().name == authored.objects.front().name));
    REQUIRE((reopened.Objects().front().localTransform == authored.objects.front().localTransform));
    REQUIRE((reopened.Objects().front().primitiveMesh == authored.objects.front().primitiveMesh));
    REQUIRE((reopened.Objects().front().components == authored.objects.front().components));
}

TEST_CASE("Scene Comparison Classifies Typed Added Removed And Modified Objects", "[unit][editor][persistence][compare]") {
    SceneObjectSnapshot documentModified{
        .id = SceneObjectId{1},
        .name = "Document Name",
        .localTransform = Math::Transform{.translation = {1.0F, 0.0F, 0.0F}},
        .primitiveMesh = PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box),
    };
    SceneObjectSnapshot diskModified = documentModified;
    diskModified.name = "Disk Name";
    diskModified.localTransform.translation = {2.0F, 0.0F, 0.0F};
    diskModified.components.light = Runtime::LightComponent{.kind = Runtime::LightKind::Point};

    const SceneDocumentSnapshot document{
        .objects =
            {
                documentModified,
                SceneObjectSnapshot{
                    .id = SceneObjectId{2},
                    .name = "Removed",
                },
            },
    };
    const SceneDocumentSnapshot disk{
        .objects =
            {
                diskModified,
                SceneObjectSnapshot{
                    .id = SceneObjectId{3},
                    .name = "Added",
                },
            },
    };

    const SceneDocumentComparison comparison = CompareSceneDocuments(document, disk);
    REQUIRE((comparison.addedOnDisk == 1));
    REQUIRE((comparison.removedFromDisk == 1));
    REQUIRE((comparison.modified == 1));
    REQUIRE((comparison.objects.size() == 3));

    const SceneObjectComparison &modified = comparison.objects[0];
    REQUIRE((modified.id == SceneObjectId{1}));
    REQUIRE((modified.kind == SceneObjectComparisonKind::Modified));
    REQUIRE((modified.documentName == "Document Name"));
    REQUIRE((modified.diskName == "Disk Name"));
    REQUIRE((modified.fields.name));
    REQUIRE((modified.fields.transform));
    REQUIRE((modified.fields.components));
    REQUIRE((!modified.fields.parent));
    REQUIRE((!modified.fields.primitive));
    REQUIRE((comparison.objects[1].kind == SceneObjectComparisonKind::RemovedFromDisk));
    REQUIRE((comparison.objects[2].kind == SceneObjectComparisonKind::AddedOnDisk));
}

TEST_CASE("Failed Atomic Scene Replace Preserves Canonical Bytes", "[unit][editor][persistence]") {
    TemporaryProject project;
    project.WriteMetadata();
    const std::string original = "{\"schemaVersion\":1,\"objects\":[]}\n";
    project.WriteScene(original);

    ReplaceFailingFileSystem files;
    ProjectMutationCoordinator mutations(files);
    auto expected = InspectProjectSceneFingerprint(project.Root(), project.ScenePath());
    REQUIRE((expected.HasValue()));
    REQUIRE((SaveProjectScene(project.Root(), project.ScenePath(), AuthoredScene(), expected.Value(), false, mutations, files).HasError()));

    std::ifstream input(project.ScenePath(), std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    REQUIRE((persisted == original));
    REQUIRE((!std::filesystem::exists(project.ScenePath().string() + ".save.tmp")));
}

TEST_CASE("Project Scene Resolver Rejects Parent Traversal", "[unit][editor][persistence]") {
    TemporaryProject project;
    project.WriteMetadata("../outside.horo");
    REQUIRE((LoadProjectDefaultScene(project.Root()).HasError()));
}

TEST_CASE("Project Scene Resolver Rejects A Missing Configured Default Scene", "[unit][editor][persistence]") {
    TemporaryProject project;
    project.WriteMetadata();
    REQUIRE((LoadProjectDefaultScene(project.Root()).HasError()));
}

TEST_CASE("Project Scene Loader Rejects Unknown Schema Versions", "[unit][editor][persistence]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":2,\"objects\":[]}\n");
    REQUIRE((LoadProjectDefaultScene(project.Root()).HasError()));
}

TEST_CASE("Scene Save Detects External Byte Changes Before Atomic Replacement", "[unit][editor][persistence][conflict]") {
    TemporaryProject project;
    project.WriteMetadata();
    const std::string original = "{\"schemaVersion\":1,\"objects\":[]}\n";
    project.WriteScene(original);

    NativeDurableFileSystem files;
    ProjectMutationCoordinator mutations(files);
    auto expected = InspectProjectSceneFingerprint(project.Root(), project.ScenePath());
    REQUIRE((expected.HasValue()));

    const std::string external = "{\n  \"schemaVersion\": 1,\n  \"objects\": []\n}\n";
    project.WriteScene(external);
    auto conflict = SaveProjectScene(project.Root(), project.ScenePath(), AuthoredScene(), expected.Value(), false, mutations, files);
    REQUIRE((conflict.HasValue()));
    REQUIRE((conflict.Value().status == ProjectSceneSaveStatus::Conflict));

    std::ifstream input(project.ScenePath(), std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    REQUIRE((bytes == external));
    REQUIRE((!std::filesystem::exists(project.ScenePath().string() + ".save.tmp")));
}

TEST_CASE("Explicit Scene Conflict Overwrite Returns The New Canonical Identity", "[unit][editor][persistence][conflict]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");

    NativeDurableFileSystem files;
    ProjectMutationCoordinator mutations(files);
    auto expected = InspectProjectSceneFingerprint(project.Root(), project.ScenePath());
    REQUIRE((expected.HasValue()));
    project.WriteScene("{\n\"schemaVersion\":1,\"objects\":[]}\n");

    auto saved = SaveProjectScene(project.Root(), project.ScenePath(), AuthoredScene(), expected.Value(), true, mutations, files);
    REQUIRE((saved.HasValue()));
    REQUIRE((saved.Value().status == ProjectSceneSaveStatus::Saved));
    auto current = InspectProjectSceneFingerprint(project.Root(), project.ScenePath());
    REQUIRE((current.HasValue()));
    REQUIRE((current.Value() == saved.Value().fingerprint));
}

TEST_CASE("Scene Destination Save Requires Explicit Existing File Approval", "[unit][editor][persistence][save-as]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");
    const std::filesystem::path destination = project.Root() / "assets/scenes/existing.horo";
    const std::string original = "{\"schemaVersion\":1,\"objects\":[]}\n";
    {
        std::ofstream output(destination, std::ios::binary);
        output << original;
    }

    NativeDurableFileSystem files;
    ProjectMutationCoordinator mutations(files);
    auto rejected = SaveProjectSceneToPath(project.Root(), destination, AuthoredScene(), false, mutations, files);
    REQUIRE((rejected.HasValue()));
    REQUIRE((rejected.Value().status == ProjectSceneDestinationSaveStatus::DestinationExists));

    std::ifstream input(destination, std::ios::binary);
    const std::string unchanged{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    REQUIRE((unchanged == original));

    auto saved = SaveProjectSceneToPath(project.Root(), destination, AuthoredScene(), true, mutations, files);
    REQUIRE((saved.HasValue()));
    REQUIRE((saved.Value().status == ProjectSceneDestinationSaveStatus::Saved));
}

TEST_CASE("Scene Destination Save Rejects Invalid Paths And Detects Races", "[unit][editor][persistence][save-as][conflict]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");
    const std::filesystem::path destination = project.Root() / "assets/scenes/copy.horo";

    NativeDurableFileSystem nativeFiles;
    ProjectMutationCoordinator nativeMutations(nativeFiles);
    REQUIRE(
        (SaveProjectSceneToPath(project.Root(), std::filesystem::path{"relative.horo"}, AuthoredScene(), true, nativeMutations, nativeFiles)
             .HasError()));
    REQUIRE((SaveProjectSceneToPath(project.Root(), project.Root() / "assets/scenes/copy.txt", AuthoredScene(), true, nativeMutations,
                                    nativeFiles)
                 .HasError()));
    REQUIRE((SaveProjectSceneToPath(project.Root(), project.Root().parent_path() / "outside.horo", AuthoredScene(), true, nativeMutations,
                                    nativeFiles)
                 .HasError()));

    const std::filesystem::path outsideDirectory = project.Root().parent_path() / (project.Root().filename().string() + "-outside");
    std::filesystem::create_directories(outsideDirectory);
    std::error_code symlinkError;
    const std::filesystem::path linkedDirectory = project.Root() / "assets/scenes/linked";
    std::filesystem::create_directory_symlink(outsideDirectory, linkedDirectory, symlinkError);
    if (!symlinkError) {
        REQUIRE(
            (SaveProjectSceneToPath(project.Root(), linkedDirectory / "escaped.horo", AuthoredScene(), true, nativeMutations, nativeFiles)
                 .HasError()));
    }
    std::error_code outsideCleanupError;
    std::filesystem::remove_all(outsideDirectory, outsideCleanupError);

    InterferingFileSystem interferingFiles(destination);
    ProjectMutationCoordinator interferingMutations(interferingFiles);
    auto conflict = SaveProjectSceneToPath(project.Root(), destination, AuthoredScene(), true, interferingMutations, interferingFiles);
    REQUIRE((conflict.HasValue()));
    REQUIRE((conflict.Value().status == ProjectSceneDestinationSaveStatus::Conflict));
    REQUIRE((!std::filesystem::exists(destination.string() + ".save.tmp")));
}

TEST_CASE("Scene Save Rechecks External Identity Immediately Before Replacement", "[unit][editor][persistence][conflict]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");

    auto expected = InspectProjectSceneFingerprint(project.Root(), project.ScenePath());
    REQUIRE((expected.HasValue()));
    InterferingFileSystem files(project.ScenePath());
    ProjectMutationCoordinator mutations(files);

    auto conflict = SaveProjectScene(project.Root(), project.ScenePath(), AuthoredScene(), expected.Value(), false, mutations, files);
    REQUIRE((conflict.HasValue()));
    REQUIRE((conflict.Value().status == ProjectSceneSaveStatus::Conflict));
    REQUIRE((!std::filesystem::exists(project.ScenePath().string() + ".save.tmp")));

    auto external = LoadProjectDefaultScene(project.Root());
    REQUIRE((external.HasValue()));
    REQUIRE((external.Value().has_value()));
    REQUIRE((external.Value()->objects.empty()));
}

TEST_CASE("Scene Recovery Round Trips Without Mutating Canonical Scene", "[unit][editor][persistence][recovery]") {
    TemporaryProject project;
    project.WriteMetadata();
    const std::string canonical = "{\"schemaVersion\":1,\"objects\":[]}\n";
    project.WriteScene(canonical);

    NativeDurableFileSystem files;
    ProjectMutationCoordinator mutations(files);
    const SceneDocumentSnapshot authored = AuthoredScene();
    REQUIRE(
        (WriteProjectSceneRecovery(project.Root(), project.ScenePath(), authored, DocumentRevision{}, DocumentStateId{1}, mutations, files)
             .HasValue()));

    std::ifstream canonicalInput(project.ScenePath(), std::ios::binary);
    const std::string canonicalAfterAutosave{std::istreambuf_iterator<char>{canonicalInput}, std::istreambuf_iterator<char>{}};
    REQUIRE((canonicalAfterAutosave == canonical));
    REQUIRE((std::filesystem::exists(project.RecoveryPath())));

    auto inspected = InspectProjectSceneRecovery(project.Root(), project.ScenePath());
    REQUIRE((inspected.HasValue()));
    REQUIRE((inspected.Value().has_value()));
    REQUIRE((inspected.Value()->absoluteCanonicalPath == project.ScenePath()));
    REQUIRE((inspected.Value()->savedRevision == DocumentRevision{}));
    REQUIRE((inspected.Value()->savedState == DocumentStateId{1}));
    REQUIRE((inspected.Value()->recoveredRevision == authored.revision));
    REQUIRE((inspected.Value()->recoveredState == authored.state));
    REQUIRE((inspected.Value()->objects.size() == authored.objects.size()));
    RequireSameSceneObject(inspected.Value()->objects.front(), authored.objects.front());

    SceneDocument restored;
    REQUIRE((restored.LoadRecovered(std::move(inspected.Value()->objects)).HasValue()));
    REQUIRE((restored.IsDirty()));
    REQUIRE((restored.Objects().size() == authored.objects.size()));
    RequireSameSceneObject(restored.Objects().front(), authored.objects.front());

    REQUIRE((DiscardProjectSceneRecovery(project.Root(), mutations, files).HasValue()));
    REQUIRE((!std::filesystem::exists(project.RecoveryPath())));
    auto absent = InspectProjectSceneRecovery(project.Root(), project.ScenePath());
    REQUIRE((absent.HasValue()));
    REQUIRE((!absent.Value().has_value()));
}

TEST_CASE("Scene Recovery Rejects Payload Whose Checksum No Longer Matches", "[unit][editor][persistence][recovery]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");

    NativeDurableFileSystem files;
    ProjectMutationCoordinator mutations(files);
    const SceneDocumentSnapshot authored = AuthoredScene();
    REQUIRE(
        (WriteProjectSceneRecovery(project.Root(), project.ScenePath(), authored, DocumentRevision{}, DocumentStateId{1}, mutations, files)
             .HasValue()));

    std::ifstream input(project.RecoveryPath(), std::ios::binary);
    std::string corrupted{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    const std::size_t name = corrupted.find("Persisted Box");
    REQUIRE((name != std::string::npos));
    corrupted.replace(name, std::string{"Persisted Box"}.size(), "Corrupted Box");
    {
        std::ofstream output(project.RecoveryPath(), std::ios::binary | std::ios::trunc);
        output << corrupted;
    }

    REQUIRE((InspectProjectSceneRecovery(project.Root(), project.ScenePath()).HasError()));
}

TEST_CASE("Failed Atomic Recovery Replace Leaves No Partial Recovery", "[unit][editor][persistence][recovery]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");

    ReplaceFailingFileSystem files;
    ProjectMutationCoordinator mutations(files);
    REQUIRE((WriteProjectSceneRecovery(project.Root(), project.ScenePath(), AuthoredScene(), DocumentRevision{}, DocumentStateId{1},
                                       mutations, files)
                 .HasError()));
    REQUIRE((!std::filesystem::exists(project.RecoveryPath())));
    REQUIRE((!std::filesystem::exists(project.RecoveryPath().string() + ".tmp")));
}

TEST_CASE("Scene File Watch Inspects Canonical Bytes Off The Owner Thread", "[unit][editor][persistence][watch]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");

    JobSystem jobs(JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 8});
    {
        SceneFileWatchService watcher(jobs);
        auto requested = watcher.Request(project.Root(), project.ScenePath());
        REQUIRE((requested.HasValue()));

        std::vector<SceneFileWatchUpdate> updates;
        for (std::size_t attempt = 0; attempt < 100'000 && updates.empty(); ++attempt) {
            updates = watcher.DrainUpdates();
            std::this_thread::yield();
        }
        REQUIRE((updates.size() == 1));
        REQUIRE((updates.front().generation == requested.Value()));
        REQUIRE((updates.front().fingerprint.has_value()));
        REQUIRE((!updates.front().error.has_value()));

        const SceneFileFingerprint first = *updates.front().fingerprint;
        project.WriteScene("{\n  \"schemaVersion\": 1,\n  \"objects\": []\n}\n");
        requested = watcher.Request(project.Root(), project.ScenePath());
        REQUIRE((requested.HasValue()));
        updates.clear();
        for (std::size_t attempt = 0; attempt < 100'000 && updates.empty(); ++attempt) {
            updates = watcher.DrainUpdates();
            std::this_thread::yield();
        }
        REQUIRE((updates.size() == 1));
        REQUIRE((updates.front().fingerprint.has_value()));
        REQUIRE((*updates.front().fingerprint != first));
    }
    jobs.Shutdown(ShutdownPolicy::Drain);
}

TEST_CASE("Scene File Watch Reset Discards Completed Stale Generation", "[unit][editor][persistence][watch]") {
    TemporaryProject project;
    project.WriteMetadata();
    project.WriteScene("{\"schemaVersion\":1,\"objects\":[]}\n");

    JobSystem jobs(JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 8});
    {
        SceneFileWatchService watcher(jobs);
        const auto staleGeneration = watcher.Request(project.Root(), project.ScenePath());
        REQUIRE((staleGeneration.HasValue()));
        for (std::size_t attempt = 0; attempt < 100'000 && watcher.HasPendingInspection(); ++attempt)
            std::this_thread::yield();
        REQUIRE((!watcher.HasPendingInspection()));

        watcher.Reset();
        REQUIRE((watcher.DrainUpdates().empty()));

        project.WriteScene("{\n  \"schemaVersion\": 1,\n  \"objects\": []\n}\n");
        const auto currentGeneration = watcher.Request(project.Root(), project.ScenePath());
        REQUIRE((currentGeneration.HasValue()));
        REQUIRE((currentGeneration.Value() > staleGeneration.Value()));

        std::vector<SceneFileWatchUpdate> updates;
        for (std::size_t attempt = 0; attempt < 100'000 && updates.empty(); ++attempt) {
            updates = watcher.DrainUpdates();
            std::this_thread::yield();
        }
        REQUIRE((updates.size() == 1));
        REQUIRE((updates.front().generation == currentGeneration.Value()));
    }
    jobs.Shutdown(ShutdownPolicy::Drain);
}
