/** @file ExternalProcessRunner.h
 *  @brief Manages the lifecycle of a single external child process spawned by the launcher. */
#pragma once

#include <memory>
#include <string>

#include "ui/launcher/LauncherProject.h"

namespace Horo::Launcher {

/** @brief Snapshot of the current state of a managed external process. */
struct ExternalProcessStatus {
    bool active = false;              /**< True while the process is running. */
    bool finished = false;           /**< True after the process has exited. */
    bool failedToStart = false;      /**< True when the OS rejected the spawn request. */
    bool terminatedByUser = false;   /**< True when Stop() was called before natural exit. */
    int exitCode = 0;                /**< Process exit code; valid only when finished is true. */
    std::string label;               /**< Human-readable label identifying the operation. */
    std::string commandLine;         /**< Full command line string for display purposes. */
    std::string error;               /**< Error message if the process failed to start. */
};

/** @brief Spawns, polls, and terminates a single external child process on behalf of the launcher. */
class ExternalProcessRunner {
public:
    /** @brief Constructs an idle runner with no active process. */
    ExternalProcessRunner();

    /** @brief Destroys the runner, forcibly stopping any active process. */
    ~ExternalProcessRunner();

    ExternalProcessRunner(const ExternalProcessRunner &) = delete;

    ExternalProcessRunner &operator=(const ExternalProcessRunner &) = delete;

    /** @brief Spawns the given command as a child process.
     *  @param command  The resolved command to execute.
     *  @param label    Human-readable label shown in the UI while the process runs.
     *  @param outError Receives a human-readable error message on failure.
     *  @return True if the process was started successfully. */
    bool Start(const ResolvedLauncherCommand &command, const std::string &label,
               std::string *outError);

    /** @brief Checks the child process state and updates the internal status; call once per frame. */
    void Poll();

    /** @brief Sends a termination signal to the child process if it is still running. */
    void Stop();

    /** @brief Returns true when a child process is currently running.
     *  @return True if an active process exists. */
    bool IsActive() const;

    /** @brief Returns the current process status snapshot.
     *  @return Const reference to the internal status struct. */
    const ExternalProcessStatus &GetStatus() const { return m_status; }

private:
    /** @brief Opaque OS-level process handle; defined in the implementation file. */
    struct ProcessHandle;
    std::unique_ptr<ProcessHandle> m_process; /**< Platform process handle; nullptr when idle. */
    ExternalProcessStatus m_status;           /**< Most recently polled process status. */

    /** @brief Records the final status once the child process has exited.
     *  @param exitCode         The process exit code.
     *  @param terminatedByUser True when the exit was triggered by Stop().
     *  @param error            Optional error description. */
    void Finish(int exitCode, bool terminatedByUser, std::string error = {});
};

} // namespace Horo::Launcher
