#include "Horo/Editor/ProjectCreationService.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/JobSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("horo-project-service-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& Path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

Horo::Editor::ProjectCreationRequest ValidRequest(const std::filesystem::path& root) {
    return {
        .templateId = "3d-starter",
        .projectName = "MyGame",
        .projectRoot = root,
        .projectVersion = "0.1.0",
        .defaultScene = "assets/scenes/main.horo",
        .renderBackend = "opengl",
        .physicsEnabled = true,
        .targetFrameRate = 60,
        .buildProfile = "desktop-debug",
        .assetCompression = "lz4",
        .textureCompression = "bc7",
        .targetPlatform = "host",
        .compilerFamily = "default",
        .minimumCxxStandard = 20,
        .initializeGit = true,
        .restorePackages = true,
        .includeStarterContent = true,
        .generateCMakeProject = true,
    };
}

Horo::Editor::ProjectCreationSnapshot WaitForTerminal(Horo::Editor::ProjectCreationService& service,
                                                      Horo::Editor::ProjectCreationOperationId id) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        const auto snapshot = service.Query(id);
        assert(snapshot.has_value());
        if (snapshot->state == Horo::Editor::ProjectCreationOperationState::Succeeded ||
            snapshot->state == Horo::Editor::ProjectCreationOperationState::Failed ||
            snapshot->state == Horo::Editor::ProjectCreationOperationState::Cancelled) {
            return *snapshot;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "project creation did not finish");
    return {};
}

void CreatesPortableProjectInAnAtomicPromotion() {
    TemporaryDirectory temporary;
    Horo::JobSystem jobs{{.workerCount = 1, .maxQueuedJobs = 4}};
    Horo::EngineDataBus bus;
    Horo::Editor::ProjectCreationService service{jobs, bus};
    const auto root = temporary.Path() / "MyGame";

    const auto started = service.StartCreate(ValidRequest(root));
    assert(started.HasValue());
    const auto snapshot = WaitForTerminal(service, started.Value().id);
    assert(snapshot.state == Horo::Editor::ProjectCreationOperationState::Succeeded);
    assert(snapshot.phase == Horo::Editor::ProjectCreationOperationPhase::Completed);
    assert(std::filesystem::is_directory(root / ".horo"));
    assert(std::filesystem::is_directory(root / "assets/models"));
    assert(std::filesystem::is_directory(root / "assets/textures"));
    assert(std::filesystem::is_directory(root / "assets/materials"));
    assert(std::filesystem::is_directory(root / "assets/shaders"));
    assert(std::filesystem::is_regular_file(root / "assets/scenes/main.horo"));
    assert(std::filesystem::is_regular_file(root / "CMakeLists.txt"));

    const std::string project = Read(root / ".horo/project.json");
    assert(project.find("\"formatVersion\": 1") != std::string::npos);
    assert(project.find("\"name\": \"MyGame\"") != std::string::npos);
    assert(project.find("\"projectVersion\": \"0.1.0\"") != std::string::npos);
    assert(project.find("\"defaultScene\": \"assets/scenes/main.horo\"") != std::string::npos);
    assert(project.find("\"minimumCxxStandard\": 20") != std::string::npos);
    assert(Read(root / ".horo/plugins.json") == "{\n  \"schemaVersion\": 1,\n  \"requestedPlugins\": []\n}\n");
    assert(Read(root / ".horo/input.json") ==
           "{\n  \"schemaVersion\": 1,\n  \"profileId\": \"project-default\",\n  \"overrides\": []\n}\n");
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

void RefusesOccupiedDestinationWithoutOverwritingIt() {
    TemporaryDirectory temporary;
    const auto root = temporary.Path() / "occupied";
    std::filesystem::create_directories(root);
    std::ofstream(root / "keep.txt") << "keep";
    Horo::JobSystem jobs{{.workerCount = 1, .maxQueuedJobs = 4}};
    Horo::EngineDataBus bus;
    Horo::Editor::ProjectCreationService service{jobs, bus};

    const auto started = service.StartCreate(ValidRequest(root));
    assert(started.HasError());
    assert(started.ErrorValue().code.Value() == "project_creation.destination_occupied");
    assert(Read(root / "keep.txt") == "keep");
    assert(!std::filesystem::exists(root / ".horo"));
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

void CancellationLeavesNoDestinationOrStagingDirectory() {
    TemporaryDirectory temporary;
    Horo::JobSystem jobs{{.workerCount = 0, .maxQueuedJobs = 4}};
    Horo::EngineDataBus bus;
    Horo::Editor::ProjectCreationService service{jobs, bus};
    std::atomic<int> createdEvents{0};
    const auto subscription = bus.Subscribe<Horo::Editor::ProjectCreatedEvent>([&](const auto&) { ++createdEvents; });
    const auto root = temporary.Path() / "Cancelled";

    const auto started = service.StartCreate(ValidRequest(root));
    assert(started.HasValue());
    assert(service.RequestCancel(started.Value().id).HasValue());
    const auto snapshot = WaitForTerminal(service, started.Value().id);
    assert(snapshot.state == Horo::Editor::ProjectCreationOperationState::Cancelled);
    assert(!std::filesystem::exists(root));
    for (const auto& entry : std::filesystem::directory_iterator(temporary.Path())) {
        assert(entry.path().filename().string().find(".horo-create-") == std::string::npos);
    }
    service.PumpMainThread();
    assert(createdEvents.load() == 0);
    jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
}

void PublishesCommittedEventsOnlyAfterMainThreadDispatch() {
    TemporaryDirectory temporary;
    Horo::JobSystem jobs{{.workerCount = 1, .maxQueuedJobs = 4}};
    Horo::EngineDataBus bus;
    Horo::Editor::ProjectCreationService service{jobs, bus};
    std::atomic<int> createdEvents{0};
    std::atomic<int> revisionEvents{0};
    const auto createdSubscription = bus.Subscribe<Horo::Editor::ProjectCreatedEvent>([&](const auto&) { ++createdEvents; });
    const auto revisionSubscription = bus.Subscribe<Horo::Editor::ProjectCreationRevisionChangedEvent>([&](const auto&) { ++revisionEvents; });

    const auto started = service.StartCreate(ValidRequest(temporary.Path() / "EventProject"));
    assert(started.HasValue());
    assert(WaitForTerminal(service, started.Value().id).state == Horo::Editor::ProjectCreationOperationState::Succeeded);
    assert(createdEvents.load() == 0);
    assert(revisionEvents.load() == 0);
    service.PumpMainThread();
    assert(createdEvents.load() == 1);
    assert(revisionEvents.load() == 1);
    service.PumpMainThread();
    assert(createdEvents.load() == 1);
    assert(revisionEvents.load() == 1);
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

} // namespace

int main() {
    CreatesPortableProjectInAnAtomicPromotion();
    RefusesOccupiedDestinationWithoutOverwritingIt();
    CancellationLeavesNoDestinationOrStagingDirectory();
    PublishesCommittedEventsOnlyAfterMainThreadDispatch();
    return 0;
}
