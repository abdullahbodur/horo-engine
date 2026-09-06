#include "Horo/Application/GameplayBuildService.h"
#include "Horo/Foundation/Platform.h"
#include "Horo/Platform/ExternalProcess.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
    using namespace Horo;
    using namespace Horo::Application;

    class TemporaryProject final {
    public:
        TemporaryProject() {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            root = std::filesystem::temp_directory_path() / ("horo-gameplay-build-test-" + std::to_string(nonce));
            std::filesystem::create_directories(root / "source/gameplay");
        }

        ~TemporaryProject() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        void WriteValid() const {
            Write(root / "CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.25)
project(HoroGameplayBuildTest LANGUAGES CXX)
find_package(HoroEngineGameplay CONFIG REQUIRED)
horo_add_gameplay_module(HoroGameGameplay
    MODULE_ID tests.gameplay_build
    SOURCES source/gameplay/GameModule.cpp source/gameplay/Movement.cpp
)
)");
            Write(root / "source/gameplay/GameModule.cpp", R"(#include <Horo/Gameplay/GameModule.h>
namespace { class Module final : public Horo::Gameplay::IGameModule {
public:
    Horo::Result<void> Register(Horo::Gameplay::GameRegistrationContext&) override { return Horo::Result<void>::Success(); }
    Horo::Result<void> Start(Horo::Gameplay::GameRuntimeContext&) override { return Horo::Result<void>::Success(); }
    void Stop(Horo::Gameplay::GameRuntimeContext&) noexcept override {}
}; }
extern "C" HORO_GAME_EXPORT Horo::Gameplay::IGameModule* CreateGameModule() noexcept { return new Module{}; }
extern "C" HORO_GAME_EXPORT void DestroyGameModule(Horo::Gameplay::IGameModule* module) noexcept { delete module; }
)");
            Write(root / "source/gameplay/Movement.cpp", R"(#include <Horo/Gameplay/NativeBehavior.h>
class Movement final : public Horo::Gameplay::IBehaviorInstance {
public:
    static Horo::Gameplay::BehaviorDescriptor DescribeBehavior() {
        Horo::Gameplay::BehaviorDescriptor descriptor;
        descriptor.displayName = "Movement";
        return descriptor;
    }
};
HORO_BEHAVIOR(Movement, "game.tests.build_movement")
)");
        }

        void BreakSource() const {
            Write(root / "source/gameplay/Movement.cpp", "this is intentionally not valid C++\n");
            // Force the follow-up build through a clean configure so the test does
            // not depend on generator-specific timestamp/change detection.
            std::error_code error;
            std::filesystem::remove_all(root / ".horo/local/build/gameplay-debug", error);
        }

        std::filesystem::path root;

    private:
        static void Write(const std::filesystem::path &path, const std::string_view bytes) {
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!stream)
                throw std::runtime_error("Unable to prepare gameplay build test project.");
        }
    };

    GameplayBuildSnapshot AwaitTerminal(GameplayBuildService &service, const GameplayBuildSessionId id) {
        for (std::size_t attempt = 0; attempt < 3000; ++attempt) {
            const std::optional<GameplayBuildSnapshot> snapshot = service.Query(id);
            REQUIRE(snapshot.has_value());
            if (snapshot->state == GameplayBuildState::Succeeded || snapshot->state == GameplayBuildState::Failed ||
                snapshot->state == GameplayBuildState::Cancelled || snapshot->state == GameplayBuildState::TimedOut)
                return *snapshot;
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        FAIL("Gameplay build session did not become terminal.");
    }

    std::string Read(const std::filesystem::path &path) {
        std::ifstream stream{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    class CompilerDiagnosticProcessRunner final : public IExternalProcessRunner {
    public:
        Result<ExternalProcessResult> Run(const ExternalProcessRequest &request, const CancellationToken &) override {
            const std::filesystem::path absoluteSource = request.workingDirectory / "source/gameplay/Movement.cpp";
            request.onOutput(
                {ProcessOutputStream::StandardError, "source/gameplay/Movement.cpp:12:7: warning: deprecated gameplay declaration"});
            request.onOutput({ProcessOutputStream::StandardError, absoluteSource.string() + ":14:3: error: invalid gameplay declaration"});
            request.onOutput(
                {ProcessOutputStream::StandardError, "source/gameplay/Movement.cpp:99999999999999999999:1: error: invalid coordinate"});
            return Result<ExternalProcessResult>::Success({ProcessTerminationReason::Exited, 1});
        }
    };
}  // namespace

TEST_CASE("Gameplay build service consumes exported SDK and preserves last success on failure", "[integration][gameplay][build]") {
    TemporaryProject project;
    project.WriteValid();
    NativeExternalProcessRunner processes;
    JobSystem jobs{{2, 16}};
    NativeDurableFileSystem files;
    BuildOutputStore output{1024};
    OperationStore operations{16, 64};
    GameplayBuildService service{processes, jobs, files, &output, &operations};
    const GameplayBuildRequest request{
        .projectRoot = project.root,
        .environment = {.gameplaySdkPackage = HORO_GAMEPLAY_SDK_PACKAGE_DIR,
                        .cxxCompiler = std::filesystem::path{HORO_GAMEPLAY_CXX_COMPILER},
                        .generator = HORO_TEST_CMAKE_GENERATOR},
        .timeouts = {.configure = std::chrono::minutes{1}, .build = std::chrono::minutes{2}},
    };

    const auto started = service.Start(request);
    REQUIRE(started.HasValue());
    const GameplayBuildSnapshot success = AwaitTerminal(service, started.Value());
    const std::string terminalError = success.error.has_value() ? success.error->message : std::string{"no terminal error"};
    INFO(terminalError);
    const std::optional<BuildOutputSnapshot> buildOutput = output.SnapshotIfChanged(0);
    REQUIRE(buildOutput.has_value());
    for (const BuildOutputRecord &record : buildOutput->records)
        INFO(record.stage + ": " + record.message);
    REQUIRE(success.state == GameplayBuildState::Succeeded);
    REQUIRE(success.operationId.has_value());
    REQUIRE_FALSE(buildOutput->records.empty());
    const auto successfulOutputSession = buildOutput->records.front().sessionId;
    REQUIRE(successfulOutputSession.has_value());
    REQUIRE(successfulOutputSession->IsValid());
    for (const BuildOutputRecord &record : buildOutput->records) {
        REQUIRE((record.sessionId == successfulOutputSession));
        REQUIRE((record.operationId == success.operationId));
    }
    REQUIRE((std::ranges::count_if(buildOutput->records, [](const BuildOutputRecord &record) {
        return record.result != BuildOutputResult::None;
    }) == 1));
    REQUIRE((buildOutput->records.back().result == BuildOutputResult::Succeeded));
    REQUIRE((buildOutput->records.back().code.Value() == "gameplay.build.succeeded"));
    REQUIRE(service.IsUpToDate(request));
    REQUIRE(std::filesystem::is_regular_file(project.root / ".horo/local/gameplay_module.json"));
    const std::filesystem::path successfulState = project.root / ".horo/local/gameplay_build_state.json";
    const std::string beforeFailure = Read(successfulState);
    REQUIRE_FALSE(beforeFailure.empty());

    project.BreakSource();
    const auto broken = service.Start(request);
    REQUIRE(broken.HasValue());
    const GameplayBuildSnapshot failure = AwaitTerminal(service, broken.Value());
    REQUIRE(failure.state == GameplayBuildState::Failed);
    REQUIRE(failure.operationId.has_value());
    const std::optional<BuildOutputSnapshot> failedOutput = output.SnapshotIfChanged(buildOutput->revision);
    REQUIRE(failedOutput.has_value());
    const auto failedOutputSession = failedOutput->records.back().sessionId;
    REQUIRE(failedOutputSession.has_value());
    REQUIRE((failedOutputSession != successfulOutputSession));
    REQUIRE((std::ranges::count_if(failedOutput->records, [&](const BuildOutputRecord &record) {
        return record.sessionId == failedOutputSession && record.result != BuildOutputResult::None;
    }) == 1));
    REQUIRE((failedOutput->records.back().result == BuildOutputResult::Failed));
    REQUIRE((failedOutput->records.back().code.Value() == "gameplay.build.failed"));
    REQUIRE((failedOutput->records.back().operationId == failure.operationId));
    REQUIRE(Read(successfulState) == beforeFailure);
    REQUIRE(std::filesystem::is_regular_file(project.root / ".horo/local/gameplay_module.json"));
    REQUIRE_FALSE(service.IsUpToDate(request));

    service.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Cancel);
}

TEST_CASE("Gameplay build output classifies bounded GCC and Clang diagnostics", "[unit][gameplay][build]") {
    TemporaryProject project;
    project.WriteValid();
    CompilerDiagnosticProcessRunner processes;
    JobSystem jobs{{1, 4}};
    NativeDurableFileSystem files;
    BuildOutputStore output{16};
    GameplayBuildService service{processes, jobs, files, &output, nullptr};
    const GameplayBuildRequest request{
        .projectRoot = project.root,
        .environment = {.gameplaySdkPackage = HORO_GAMEPLAY_SDK_PACKAGE_DIR},
    };

    const auto started = service.Start(request);
    REQUIRE(started.HasValue());
    REQUIRE((AwaitTerminal(service, started.Value()).state == GameplayBuildState::Failed));
    const auto snapshot = output.SnapshotIfChanged(0);
    REQUIRE(snapshot.has_value());

    const auto findCode = [&](const std::string_view code) {
        return std::ranges::find_if(snapshot->records, [&](const BuildOutputRecord &record) {
            return record.code.Value() == code;
        });
    };
    const auto warning = findCode("gameplay.build.compiler_warning");
    REQUIRE((warning != snapshot->records.end()));
    REQUIRE((warning->severity == DiagnosticSeverity::Warning));
    REQUIRE(warning->source.has_value());
    REQUIRE((std::filesystem::path{warning->source->absolutePath} == (project.root / "source/gameplay/Movement.cpp").lexically_normal()));
    REQUIRE((warning->source->line == 12U));
    REQUIRE((warning->source->column == 7U));

    const auto error = findCode("gameplay.build.compiler_error");
    REQUIRE((error != snapshot->records.end()));
    REQUIRE((error->severity == DiagnosticSeverity::Error));
    REQUIRE(error->source.has_value());
    REQUIRE(std::filesystem::path{error->source->absolutePath}.is_absolute());
    REQUIRE((error->source->line == 14U));
    REQUIRE((error->source->column == 3U));

    const auto malformed = std::ranges::find_if(snapshot->records, [](const BuildOutputRecord &record) {
        return record.message.find("invalid coordinate") != std::string::npos;
    });
    REQUIRE((malformed != snapshot->records.end()));
    REQUIRE_FALSE(malformed->source.has_value());
    REQUIRE((malformed->severity == DiagnosticSeverity::Note));
    REQUIRE((malformed->code.Value() == "gameplay.build.output"));

    service.Shutdown();
    jobs.Shutdown(ShutdownPolicy::Cancel);
}
