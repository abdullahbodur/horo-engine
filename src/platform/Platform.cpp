#include "Horo/Foundation/Platform.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Horo
{
    namespace
    {
        const ErrorDomainId PlatformDomain{"horo.platform.filesystem"};
        const ErrorCodeDescriptor IoFailed{.domain = PlatformDomain, .code = ErrorCode{"filesystem.io_failed"},
            .defaultSeverity = ErrorSeverity::Error, .summary = "Durable filesystem operation failed.",
            .remediationHint = "Check filesystem permissions and available storage.", .retryable = true,
            .userActionable = true};
        const ErrorCodeDescriptor LockBusy{.domain = PlatformDomain, .code = ErrorCode{"filesystem.lock_busy"},
            .defaultSeverity = ErrorSeverity::Error, .summary = "The file is locked by another operation.",
            .remediationHint = "Wait for the other project mutation to finish.", .retryable = true,
            .userActionable = true};

        std::mutex ProcessLockMutex;
        std::unordered_set<std::string> ProcessLocks;

        [[nodiscard]] Error FsError(const ErrorCodeDescriptor& code, const std::filesystem::path& path)
        {
            return MakeError(code, std::string(code.summary) + " Path: " + path.generic_string());
        }

        [[nodiscard]] std::string LockKey(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto parent = std::filesystem::weakly_canonical(path.parent_path(), error);
            return (error ? path.lexically_normal() : parent / path.filename()).generic_string();
        }

#if !defined(_WIN32)
        [[nodiscard]] bool FlushFileDescriptor(const int descriptor)
        {
#if defined(__APPLE__) && defined(F_FULLFSYNC)
            if (fcntl(descriptor, F_FULLFSYNC) == 0) return true;
#endif
            return fsync(descriptor) == 0;
        }
#endif
    }

    struct ExclusiveFileLock::State
    {
        std::string processKey;
#if defined(_WIN32)
        HANDLE handle{INVALID_HANDLE_VALUE};
#else
        int descriptor{-1};
#endif

        ~State()
        {
#if defined(_WIN32)
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
            if (descriptor >= 0)
            {
                struct flock unlock{};
                unlock.l_type = F_UNLCK;
                unlock.l_whence = SEEK_SET;
                static_cast<void>(fcntl(descriptor, F_SETLK, &unlock));
                close(descriptor);
            }
#endif
            std::lock_guard lock(ProcessLockMutex);
            ProcessLocks.erase(processKey);
        }
    };

    ExclusiveFileLock::ExclusiveFileLock() noexcept = default;
    ExclusiveFileLock::ExclusiveFileLock(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
    ExclusiveFileLock::~ExclusiveFileLock() = default;
    ExclusiveFileLock::ExclusiveFileLock(ExclusiveFileLock&&) noexcept = default;
    ExclusiveFileLock& ExclusiveFileLock::operator=(ExclusiveFileLock&&) noexcept = default;
    ExclusiveFileLock::operator bool() const noexcept { return state_ != nullptr; }

    /** @copydoc DurableFileSystem::TryAcquireExclusive */
    Result<ExclusiveFileLock> NativeDurableFileSystem::TryAcquireExclusive(
        const std::filesystem::path& path, const std::string_view ownerMetadata)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return Result<ExclusiveFileLock>::Failure(FsError(IoFailed, path));
        const std::string key = LockKey(path);
        {
            std::lock_guard lock(ProcessLockMutex);
            if (!ProcessLocks.emplace(key).second)
                return Result<ExclusiveFileLock>::Failure(FsError(LockBusy, path));
        }
        auto state = std::make_unique<ExclusiveFileLock::State>();
        state->processKey = key;
#if defined(_WIN32)
        state->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (state->handle == INVALID_HANDLE_VALUE)
            return Result<ExclusiveFileLock>::Failure(FsError(
                GetLastError() == ERROR_SHARING_VIOLATION ? LockBusy : IoFailed, path));
        LARGE_INTEGER zero{};
        SetFilePointerEx(state->handle, zero, nullptr, FILE_BEGIN);
        SetEndOfFile(state->handle);
        DWORD written{};
        WriteFile(state->handle, ownerMetadata.data(), static_cast<DWORD>(ownerMetadata.size()), &written, nullptr);
        FlushFileBuffers(state->handle);
#else
        state->descriptor = open(path.c_str(), O_RDWR | O_CREAT, 0600);
        if (state->descriptor < 0)
            return Result<ExclusiveFileLock>::Failure(FsError(IoFailed, path));
        struct flock lock{};
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        if (fcntl(state->descriptor, F_SETLK, &lock) != 0)
            return Result<ExclusiveFileLock>::Failure(FsError(LockBusy, path));
        if (ftruncate(state->descriptor, 0) != 0 ||
            (!ownerMetadata.empty() && write(state->descriptor, ownerMetadata.data(), ownerMetadata.size()) < 0) ||
            !FlushFileDescriptor(state->descriptor))
            return Result<ExclusiveFileLock>::Failure(FsError(IoFailed, path));
#endif
        return Result<ExclusiveFileLock>::Success(ExclusiveFileLock(std::move(state)));
    }

    /** @copydoc DurableFileSystem::AvailableBytes */
    Result<std::uint64_t> NativeDurableFileSystem::AvailableBytes(const std::filesystem::path& path) const
    {
        std::error_code error;
        const auto info = std::filesystem::space(path, error);
        if (error) return Result<std::uint64_t>::Failure(FsError(IoFailed, path));
        return Result<std::uint64_t>::Success(info.available);
    }

    /** @copydoc DurableFileSystem::WriteDurable */
    Result<void> NativeDurableFileSystem::WriteDurable(
        const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return Result<void>::Failure(FsError(IoFailed, path));
#if defined(_WIN32)
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return Result<void>::Failure(FsError(IoFailed, path));
        DWORD written{};
        const bool ok = bytes.size() <= MAXDWORD &&
            WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
            written == bytes.size() && FlushFileBuffers(handle);
        CloseHandle(handle);
        if (!ok) return Result<void>::Failure(FsError(IoFailed, path));
#else
        const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (descriptor < 0) return Result<void>::Failure(FsError(IoFailed, path));
        std::size_t offset = 0;
        bool ok = true;
        while (offset < bytes.size())
        {
            const ssize_t count = write(descriptor, bytes.data() + offset, bytes.size() - offset);
            if (count <= 0) { ok = false; break; }
            offset += static_cast<std::size_t>(count);
        }
        ok = ok && FlushFileDescriptor(descriptor);
        close(descriptor);
        if (!ok) return Result<void>::Failure(FsError(IoFailed, path));
#endif
        return SyncDirectory(path.parent_path());
    }

    /** @copydoc DurableFileSystem::CopyDurable */
    Result<void> NativeDurableFileSystem::CopyDurable(
        const std::filesystem::path& source, const std::filesystem::path& destination)
    {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error || !std::filesystem::copy_file(
                source, destination, std::filesystem::copy_options::overwrite_existing, error))
            return Result<void>::Failure(FsError(IoFailed, error ? destination : source));
#if defined(_WIN32)
        HANDLE handle = CreateFileW(destination.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool flushed = handle != INVALID_HANDLE_VALUE && FlushFileBuffers(handle);
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        const int descriptor = open(destination.c_str(), O_RDONLY);
        const bool flushed = descriptor >= 0 && FlushFileDescriptor(descriptor);
        if (descriptor >= 0) close(descriptor);
#endif
        if (!flushed) return Result<void>::Failure(FsError(IoFailed, destination));
        return SyncDirectory(destination.parent_path());
    }

    /** @copydoc DurableFileSystem::AtomicReplace */
    Result<void> NativeDurableFileSystem::AtomicReplace(
        const std::filesystem::path& prepared, const std::filesystem::path& destination)
    {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) return Result<void>::Failure(FsError(IoFailed, destination));
#if defined(_WIN32)
        const bool exists = std::filesystem::exists(destination, error);
        BOOL ok = exists ? ReplaceFileW(destination.c_str(), prepared.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH,
                                        nullptr, nullptr)
                         : MoveFileExW(prepared.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
        if (!ok) return Result<void>::Failure(FsError(IoFailed, destination));
#else
        std::filesystem::rename(prepared, destination, error);
        if (error) return Result<void>::Failure(FsError(IoFailed, destination));
#endif
        return SyncDirectory(destination.parent_path());
    }

    /** @copydoc DurableFileSystem::RemoveDurable */
    Result<void> NativeDurableFileSystem::RemoveDurable(const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::remove(path, error) && error)
            return Result<void>::Failure(FsError(IoFailed, path));
        return SyncDirectory(path.parent_path());
    }

    /** @copydoc DurableFileSystem::SyncDirectory */
    Result<void> NativeDurableFileSystem::SyncDirectory(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        static_cast<void>(path);
        return Result<void>::Success();
#else
        const int descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY);
        if (descriptor < 0) return Result<void>::Failure(FsError(IoFailed, path));
        const bool ok = fsync(descriptor) == 0;
        close(descriptor);
        return ok ? Result<void>::Success() : Result<void>::Failure(FsError(IoFailed, path));
#endif
    }
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

    /** @copydoc WallClock::UtcNow */
    std::chrono::system_clock::time_point SystemWallClock::UtcNow() const
    {
        return std::chrono::system_clock::now();
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
