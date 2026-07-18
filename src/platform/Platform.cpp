#include "Horo/Foundation/Platform.h"

namespace Horo
{
    /** @copydoc PlatformServices::PlatformServices */
    PlatformServices::PlatformServices(FileSystem& files, Clock& clock, ProcessService& processes,
        UserDirectories& directories, const PlatformCapabilities capabilities, CredentialStore* credentials,
        NativeDialogs* dialogs, CrashService* crash) noexcept
        : files(files)
        , clock(clock)
        , processes(processes)
        , directories(directories)
        , credentials(credentials)
        , dialogs(dialogs)
        , crash(crash)
        , m_capabilities(capabilities)
    {
        m_capabilities.hasCredentialStore = credentials != nullptr;
        m_capabilities.hasNativeDialogs = dialogs != nullptr;
        m_capabilities.hasCrashService = crash != nullptr;
    }

    /** @copydoc PlatformServices::Capabilities */
    const PlatformCapabilities& PlatformServices::Capabilities() const noexcept
    {
        return m_capabilities;
    }

    /** @copydoc FileSystem::Exists */
    bool NullFileSystem::Exists(const std::filesystem::path& path) const
    {
        static_cast<void>(path);
        return false;
    }

    /** @copydoc DeterministicClock::DeterministicClock */
    DeterministicClock::DeterministicClock(const Duration initial) noexcept
        : m_now(initial)
    {
    }

    /** @copydoc Clock::MonotonicNow */
    Duration DeterministicClock::MonotonicNow() const
    {
        return m_now;
    }

    /** @copydoc DeterministicClock::Advance */
    void DeterministicClock::Advance(const Duration elapsed) noexcept
    {
        m_now = m_now + elapsed;
    }

    /** @copydoc SteadyClock::MonotonicNow */
    Duration SteadyClock::MonotonicNow() const
    {
        return Duration::FromNanoseconds(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    /** @copydoc ProcessService::CurrentProcess */
    ProcessMetadata NullProcessService::CurrentProcess() const
    {
        return {};
    }

    /** @copydoc ProcessService::EnvironmentValue */
    std::optional<std::string> NullProcessService::EnvironmentValue(const std::string_view name) const
    {
        static_cast<void>(name);
        return std::nullopt;
    }

    /** @copydoc StaticUserDirectories::StaticUserDirectories */
    StaticUserDirectories::StaticUserDirectories(UserDirectoryPaths paths)
        : m_paths(std::move(paths))
    {
    }

    /** @copydoc UserDirectories::Config */
    const std::filesystem::path& StaticUserDirectories::Config() const
    {
        return m_paths.config;
    }

    /** @copydoc UserDirectories::State */
    const std::filesystem::path& StaticUserDirectories::State() const
    {
        return m_paths.state;
    }

    /** @copydoc UserDirectories::Cache */
    const std::filesystem::path& StaticUserDirectories::Cache() const
    {
        return m_paths.cache;
    }

    /** @copydoc UserDirectories::Logs */
    const std::filesystem::path& StaticUserDirectories::Logs() const
    {
        return m_paths.logs;
    }

    /** @copydoc UserDirectories::Crash */
    const std::filesystem::path& StaticUserDirectories::Crash() const
    {
        return m_paths.crash;
    }

    /** @copydoc UserDirectories::Temporary */
    const std::filesystem::path& StaticUserDirectories::Temporary() const
    {
        return m_paths.temporary;
    }
} // namespace Horo
