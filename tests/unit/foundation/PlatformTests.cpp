#include "Horo/Foundation/Platform.h"
#include "Horo/Platform/ExternalProcess.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
    class RecordingFileSystem final : public Horo::FileSystem {
    public:
        [[nodiscard]] bool Exists(const std::filesystem::path &path) const override {
            lastPath = path;
            return exists;
        }

        bool exists{true};
        mutable std::filesystem::path lastPath;
    };

    class FixedProcessService final : public Horo::ProcessService {
    public:
        [[nodiscard]] Horo::ProcessMetadata CurrentProcess() const override {
            return Horo::ProcessMetadata{.id = 42, .executableName = "test-host"};
        }

        [[nodiscard]] std::optional<std::string> EnvironmentValue(const std::string_view name) const override {
            if (name == "HORO_TEST")
                return std::string{"enabled"};
            return std::nullopt;
        }
    };

    TEST_CASE("Platform Services Use Explicitly Injected Baseline Services", "[unit][foundation]") {
        RecordingFileSystem files;
        Horo::DeterministicClock clock(Horo::Duration::FromMilliseconds(10));
        FixedProcessService processes;
        Horo::StaticUserDirectories directories({
            .config = "/test/config",
            .state = "/test/state",
            .cache = "/test/cache",
            .logs = "/test/logs",
            .crash = "/test/crash",
            .temporary = "/test/tmp",
        });

        Horo::PlatformServices services(files, clock, processes, directories);

        REQUIRE((services.files.Exists("/test/project.horo")));
        REQUIRE((files.lastPath == "/test/project.horo"));
        REQUIRE((services.clock.MonotonicNow().ToMilliseconds() == 10));
        REQUIRE((services.processes.CurrentProcess().id == 42));
        REQUIRE((services.processes.EnvironmentValue("HORO_TEST") == "enabled"));
        REQUIRE((services.directories.Config() == "/test/config"));
    }

    TEST_CASE("Platform Capabilities Report Optional Service Availability", "[unit][foundation]") {
        Horo::NullFileSystem files;
        Horo::DeterministicClock clock;
        Horo::NullProcessService processes;
        Horo::StaticUserDirectories directories({});
        constexpr Horo::PlatformCapabilities capabilities{
            .supportsProcessExecution = false,
            .hasCredentialStore = false,
            .hasNativeDialogs = false,
            .hasCrashService = false,
        };

        const Horo::PlatformServices services(files, clock, processes, directories, capabilities);

        REQUIRE((!services.Capabilities().supportsProcessExecution));
        REQUIRE((!services.Capabilities().hasCredentialStore));
        REQUIRE((!services.Capabilities().hasNativeDialogs));
        REQUIRE((!services.Capabilities().hasCrashService));
    }

    TEST_CASE("Deterministic Adapters Provide Stable Clock And Paths", "[unit][foundation]") {
        Horo::DeterministicClock clock(Horo::Duration::FromMilliseconds(100));
        Horo::StaticUserDirectories directories({.temporary = "/test/tmp"});
        Horo::NullFileSystem files;

        REQUIRE((clock.MonotonicNow().ToMilliseconds() == 100));
        clock.Advance(Horo::Duration::FromMilliseconds(25));
        REQUIRE((clock.MonotonicNow().ToMilliseconds() == 125));
        REQUIRE((directories.Temporary() == "/test/tmp"));
        REQUIRE((!files.Exists("/test/missing")));
    }

    TEST_CASE("Steady Clock Is Monotonic", "[unit][foundation]") {
        const Horo::SteadyClock clock;
        const Horo::Duration first = clock.MonotonicNow();
        const Horo::Duration second = clock.MonotonicNow();
        REQUIRE((second >= first));
    }

    TEST_CASE("Native Durable Filesystem Serializes Locks And Replaces Files", "[unit][foundation]") {
        const auto root = std::filesystem::temp_directory_path() / "horo-platform-durable-test";
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::create_directories(root);
        Horo::NativeDurableFileSystem files;
        {
            auto first = files.TryAcquireExclusive(root / "mutation.lock", "first");
            REQUIRE((first.HasValue()));
            auto second = files.TryAcquireExclusive(root / "mutation.lock", "second");
            REQUIRE((second.HasError()));
        }
        REQUIRE((files.TryAcquireExclusive(root / "mutation.lock", "after-release").HasValue()));
        const std::string text = "durable";
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        REQUIRE((files.WriteDurable(root / "prepared", bytes).HasValue()));
        REQUIRE((files.AtomicReplace(root / "prepared", root / "published").HasValue()));
        std::ifstream input(root / "published", std::ios::binary);
        REQUIRE((std::string(std::istreambuf_iterator<char>(input), {}) == text));
        REQUIRE((files.AvailableBytes(root).HasValue()));
        REQUIRE((files.RemoveDurable(root / "published").HasValue()));
        std::filesystem::remove_all(root, ignored);
    }

#if !defined(_WIN32)
    TEST_CASE("External Process Runner Keeps Arguments Out Of A Shell", "[unit][platform][process]") {
        Horo::NativeExternalProcessRunner runner;
        std::vector<Horo::ProcessOutputLine> output;
        Horo::ExternalProcessRequest request{
            .executable = "/usr/bin/printf",
            .arguments = {"%s", "literal;$(touch should-not-exist)"},
            .environment = {.base = Horo::ProcessEnvironmentBase::Replace},
            .onOutput =
                [&output](Horo::ProcessOutputLine line) {
            output.push_back(std::move(line));
        },
        };

        const auto result = runner.Run(request, {});

        REQUIRE((result.HasValue()));
        REQUIRE((result.Value().reason == Horo::ProcessTerminationReason::Exited));
        REQUIRE((result.Value().exitCode == 0));
        REQUIRE((output.size() == 1));
        REQUIRE((output.front().text == "literal;$(touch should-not-exist)"));
    }

    TEST_CASE("External Process Runner Applies Replacement Environment", "[unit][platform][process]") {
        Horo::NativeExternalProcessRunner runner;
        std::vector<std::string> output;
        Horo::ExternalProcessRequest request{
            .executable = "/usr/bin/env",
            .environment =
                {
                    .base = Horo::ProcessEnvironmentBase::Replace,
                    .set = {{"HORO_PROCESS_TEST", "deterministic"}},
                },
            .onOutput =
                [&output](Horo::ProcessOutputLine line) {
            output.push_back(std::move(line.text));
        },
        };

        const auto result = runner.Run(request, {});

        REQUIRE((result.HasValue()));
        REQUIRE((output == std::vector<std::string>{"HORO_PROCESS_TEST=deterministic"}));
    }

    TEST_CASE("External Process Runner Bounds Lines And Times Out Process Group", "[unit][platform][process]") {
        Horo::NativeExternalProcessRunner runner;
        std::vector<Horo::ProcessOutputLine> output;
        Horo::ExternalProcessRequest bounded{
            .executable = "/usr/bin/printf",
            .arguments = {"%s", std::string(128, 'x')},
            .environment = {.base = Horo::ProcessEnvironmentBase::Replace},
            .maximumLineBytes = 16,
            .onOutput =
                [&output](Horo::ProcessOutputLine line) {
            output.push_back(std::move(line));
        },
        };
        const auto boundedResult = runner.Run(bounded, {});
        REQUIRE((boundedResult.HasValue()));
        REQUIRE((output.size() == 1));
        REQUIRE((output.front().text.size() == 16));
        REQUIRE((output.front().truncated));

        Horo::ExternalProcessRequest timeout{
            .executable = "/bin/sleep",
            .arguments = {"5"},
            .environment = {.base = Horo::ProcessEnvironmentBase::Replace},
            .timeout = std::chrono::milliseconds{20},
            .gracefulTermination = std::chrono::milliseconds{20},
        };
        const auto timedOut = runner.Run(timeout, {});
        REQUIRE((timedOut.HasValue()));
        REQUIRE((timedOut.Value().reason == Horo::ProcessTerminationReason::TimedOut));
    }
#endif
}  // namespace
