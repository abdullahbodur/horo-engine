#pragma once

#include "Horo/Foundation/Time.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Horo
{
    /**
     * @file Platform.h
     * @brief Narrow operating-system service contracts selected by a host composition root.
     */

    /** @brief Reports availability of optional platform facilities for a composed host. */
    struct PlatformCapabilities
    {
        bool supportsProcessExecution{false}; /**< Whether the host permits process execution. */
        bool hasCredentialStore{false};       /**< Whether a credential store was composed. */
        bool hasNativeDialogs{false};         /**< Whether native dialogs were composed. */
        bool hasCrashService{false};          /**< Whether crash reporting was composed. */
    };

    /** @brief Provides the smallest filesystem query needed by initial platform consumers. */
    class FileSystem
    {
    public:
        virtual ~FileSystem() = default;

        /** @brief Tests whether a native path exists. @param path Native path to query. @return True when the path exists. */
        [[nodiscard]] virtual bool Exists(const std::filesystem::path& path) const = 0;
    };

    /** @brief Provides monotonic time for scheduling without exposing wall-clock time. */
    class Clock
    {
    public:
        virtual ~Clock() = default;

        /** @brief Gets the elapsed monotonic time from an implementation-defined origin. @return Monotonic elapsed time. */
        [[nodiscard]] virtual Duration MonotonicNow() const = 0;
    };

    /** @brief Identifies the current host process without exposing native handles. */
    struct ProcessMetadata
    {
        std::uint64_t id{};           /**< Stable process identifier within the host OS. */
        std::string executableName;   /**< Host-provided executable display name. */
    };

    /** @brief Supplies read-only process metadata and environment access to portable code. */
    class ProcessService
    {
    public:
        virtual ~ProcessService() = default;

        /** @brief Gets metadata for the current process. @return Current process metadata. */
        [[nodiscard]] virtual ProcessMetadata CurrentProcess() const = 0;

        /** @brief Looks up a single environment value. @param name Variable name. @return Value when exposed by host policy. */
        [[nodiscard]] virtual std::optional<std::string> EnvironmentValue(std::string_view name) const = 0;
    };

    /** @brief Names the user-writable directory roots resolved by a platform host. */
    struct UserDirectoryPaths
    {
        std::filesystem::path config;
        std::filesystem::path state;
        std::filesystem::path cache;
        std::filesystem::path logs;
        std::filesystem::path crash;
        std::filesystem::path temporary;
    };

    /** @brief Resolves logical user-data directory roots without exposing environment conventions. */
    class UserDirectories
    {
    public:
        virtual ~UserDirectories() = default;

        /** @brief Gets the configuration directory. @return Resolved native path. */
        [[nodiscard]] virtual const std::filesystem::path& Config() const = 0;
        /** @brief Gets the persistent state directory. @return Resolved native path. */
        [[nodiscard]] virtual const std::filesystem::path& State() const = 0;
        /** @brief Gets the cache directory. @return Resolved native path. */
        [[nodiscard]] virtual const std::filesystem::path& Cache() const = 0;
        /** @brief Gets the log directory. @return Resolved native path. */
        [[nodiscard]] virtual const std::filesystem::path& Logs() const = 0;
        /** @brief Gets the crash-data directory. @return Resolved native path. */
        [[nodiscard]] virtual const std::filesystem::path& Crash() const = 0;
        /** @brief Gets the temporary directory. @return Resolved native path. */
        [[nodiscard]] virtual const std::filesystem::path& Temporary() const = 0;
    };

    class CredentialStore;
    class NativeDialogs;
    class CrashService;

    /** @brief Explicitly composed baseline and optional platform services for one host lifetime. */
    class PlatformServices
    {
    public:
        /**
         * @brief Constructs a platform service bundle from host-owned services.
         * @param files Filesystem implementation.
         * @param clock Monotonic clock implementation.
         * @param processes Process metadata implementation.
         * @param directories User-directory implementation.
         * @param capabilities Host policy capabilities; optional service availability is derived from supplied pointers.
         * @param credentials Optional credential store.
         * @param dialogs Optional native dialog service.
         * @param crash Optional crash service.
         */
        PlatformServices(FileSystem& files, Clock& clock, ProcessService& processes, UserDirectories& directories,
            PlatformCapabilities capabilities = {}, CredentialStore* credentials = nullptr,
            NativeDialogs* dialogs = nullptr, CrashService* crash = nullptr) noexcept;

        /** @brief Gets the capabilities declared by the composition root. @return Capability values. */
        [[nodiscard]] const PlatformCapabilities& Capabilities() const noexcept;

        FileSystem& files;
        Clock& clock;
        ProcessService& processes;
        UserDirectories& directories;
        CredentialStore* credentials;
        NativeDialogs* dialogs;
        CrashService* crash;

    private:
        PlatformCapabilities m_capabilities;
    };

    /** @brief Headless filesystem adapter that reports every path as absent. */
    class NullFileSystem final : public FileSystem
    {
    public:
        /** @copydoc FileSystem::Exists */
        [[nodiscard]] bool Exists(const std::filesystem::path& path) const override;
    };

    /** @brief Test clock whose time advances only by explicit caller action. */
    class DeterministicClock final : public Clock
    {
    public:
        /** @brief Constructs a clock at an explicit monotonic time. @param initial Initial elapsed time. */
        explicit DeterministicClock(Duration initial = Duration::FromMilliseconds(0)) noexcept;

        /** @copydoc Clock::MonotonicNow */
        [[nodiscard]] Duration MonotonicNow() const override;
        /** @brief Advances the clock without reading host time. @param elapsed Duration to add. */
        void Advance(Duration elapsed) noexcept;

    private:
        Duration m_now;
    };

    /** @brief Headless process adapter that exposes no process identity or environment variables. */
    class NullProcessService final : public ProcessService
    {
    public:
        /** @copydoc ProcessService::CurrentProcess */
        [[nodiscard]] ProcessMetadata CurrentProcess() const override;
        /** @copydoc ProcessService::EnvironmentValue */
        [[nodiscard]] std::optional<std::string> EnvironmentValue(std::string_view name) const override;
    };

    /** @brief Deterministic user-directory adapter backed by explicitly supplied paths. */
    class StaticUserDirectories final : public UserDirectories
    {
    public:
        /** @brief Stores the supplied logical directory roots. @param paths Paths to expose. */
        explicit StaticUserDirectories(UserDirectoryPaths paths);

        /** @copydoc UserDirectories::Config */
        [[nodiscard]] const std::filesystem::path& Config() const override;
        /** @copydoc UserDirectories::State */
        [[nodiscard]] const std::filesystem::path& State() const override;
        /** @copydoc UserDirectories::Cache */
        [[nodiscard]] const std::filesystem::path& Cache() const override;
        /** @copydoc UserDirectories::Logs */
        [[nodiscard]] const std::filesystem::path& Logs() const override;
        /** @copydoc UserDirectories::Crash */
        [[nodiscard]] const std::filesystem::path& Crash() const override;
        /** @copydoc UserDirectories::Temporary */
        [[nodiscard]] const std::filesystem::path& Temporary() const override;

    private:
        UserDirectoryPaths m_paths;
    };
} // namespace Horo
