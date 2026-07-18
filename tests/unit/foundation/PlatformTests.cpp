#include "Horo/Foundation/Platform.h"

#include <cassert>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace
{

class RecordingFileSystem final : public Horo::FileSystem
{
public:
    [[nodiscard]] bool Exists(const std::filesystem::path& path) const override
    {
        lastPath = path;
        return exists;
    }

    bool exists{true};
    mutable std::filesystem::path lastPath;
};

class FixedProcessService final : public Horo::ProcessService
{
public:
    [[nodiscard]] Horo::ProcessMetadata CurrentProcess() const override
    {
        return Horo::ProcessMetadata{.id = 42, .executableName = "test-host"};
    }

    [[nodiscard]] std::optional<std::string> EnvironmentValue(const std::string_view name) const override
    {
        if (name == "HORO_TEST") return std::string{"enabled"};
        return std::nullopt;
    }
};

void PlatformServicesUseExplicitlyInjectedBaselineServices()
{
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

    assert(services.files.Exists("/test/project.horo"));
    assert(files.lastPath == "/test/project.horo");
    assert(services.clock.MonotonicNow().ToMilliseconds() == 10);
    assert(services.processes.CurrentProcess().id == 42);
    assert(services.processes.EnvironmentValue("HORO_TEST") == "enabled");
    assert(services.directories.Config() == "/test/config");
}

void PlatformCapabilitiesReportOptionalServiceAvailability()
{
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

    assert(!services.Capabilities().supportsProcessExecution);
    assert(!services.Capabilities().hasCredentialStore);
    assert(!services.Capabilities().hasNativeDialogs);
    assert(!services.Capabilities().hasCrashService);
}

void DeterministicAdaptersProvideStableClockAndPaths()
{
    Horo::DeterministicClock clock(Horo::Duration::FromMilliseconds(100));
    Horo::StaticUserDirectories directories({.temporary = "/test/tmp"});
    Horo::NullFileSystem files;

    assert(clock.MonotonicNow().ToMilliseconds() == 100);
    clock.Advance(Horo::Duration::FromMilliseconds(25));
    assert(clock.MonotonicNow().ToMilliseconds() == 125);
    assert(directories.Temporary() == "/test/tmp");
    assert(!files.Exists("/test/missing"));
}

void SteadyClockIsMonotonic()
{
    const Horo::SteadyClock clock;
    const Horo::Duration first = clock.MonotonicNow();
    const Horo::Duration second = clock.MonotonicNow();
    assert(second >= first);
}

} // namespace

int main()
{
    PlatformServicesUseExplicitlyInjectedBaselineServices();
    PlatformCapabilitiesReportOptionalServiceAvailability();
    DeterministicAdaptersProvideStableClockAndPaths();
    SteadyClockIsMonotonic();
    return 0;
}
