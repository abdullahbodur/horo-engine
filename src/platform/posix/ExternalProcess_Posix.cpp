#if defined(__GLIBC__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "Horo/Platform/ExternalProcess.h"

#include "Horo/Platform/PlatformErrors.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <poll.h>
#include <spawn.h>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace Horo {
    namespace {
        class LineDecoder final {
        public:
            LineDecoder(const ProcessOutputStream stream, const std::size_t maximum,
                        const std::function<void(ProcessOutputLine)> &callback)
                : stream_(stream), maximum_(std::max<std::size_t>(maximum, 1U)), callback_(&callback) {}

            void Append(const char *bytes, const std::size_t count) {
                for (std::size_t index = 0; index < count; ++index) {
                    const char value = bytes[index];
                    if (value == '\n')
                        Emit();
                    else if (value != '\r') {
                        if (pending_.size() < maximum_)
                            pending_.push_back(value);
                        else
                            truncated_ = true;
                    }
                }
            }

            void Finish() {
                if (!pending_.empty() || truncated_)
                    Emit();
            }

        private:
            void Emit() {
                if (*callback_)
                    (*callback_)(ProcessOutputLine{stream_, std::move(pending_), truncated_});
                pending_.clear();
                truncated_ = false;
            }

            ProcessOutputStream stream_;
            std::size_t maximum_;
            const std::function<void(ProcessOutputLine)> *callback_;
            std::string pending_;
            bool truncated_{false};
        };

        [[nodiscard]] std::vector<std::string> BuildEnvironment(const ProcessEnvironment &overlay) {
            std::map<std::string, std::string, std::less<>> values;
            if (overlay.base == ProcessEnvironmentBase::InheritWithOverrides) {
                for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
                    const std::string_view text{*entry};
                    const std::size_t separator = text.find('=');
                    if (separator != std::string_view::npos)
                        values.emplace(std::string{text.substr(0, separator)}, std::string{text.substr(separator + 1)});
                }
            }
            for (const std::string &name : overlay.unset)
                values.erase(name);
            for (const ProcessEnvironmentAssignment &assignment : overlay.set)
                values[assignment.name] = assignment.value;
            std::vector<std::string> result;
            result.reserve(values.size());
            for (const auto &[name, value] : values)
                result.push_back(name + "=" + value);
            return result;
        }

        void Drain(const int descriptor, bool &open, LineDecoder &decoder) {
            std::array<char, 4096> buffer{};
            for (;;) {
                const ssize_t count = read(descriptor, buffer.data(), buffer.size());
                if (count > 0) {
                    decoder.Append(buffer.data(), static_cast<std::size_t>(count));
                    continue;
                }
                if (count == 0) {
                    open = false;
                    decoder.Finish();
                } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    open = false;
                    decoder.Finish();
                }
                return;
            }
        }
    }  // namespace

    /** @copydoc NativeExternalProcessRunner::Run */
    Result<ExternalProcessResult> NativeExternalProcessRunner::Run(const ExternalProcessRequest &request,
                                                                    const CancellationToken &cancellation) {
        if (request.executable.empty())
            return Result<ExternalProcessResult>::Failure(MakeError(PlatformErrors::ProcessLaunchFailed, "Executable is empty."));

        int stdoutPipe[2]{-1, -1};
        int stderrPipe[2]{-1, -1};
        if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
            for (const int descriptor : {stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1]})
                if (descriptor >= 0)
                    close(descriptor);
            return Result<ExternalProcessResult>::Failure(MakeError(PlatformErrors::ProcessIoFailed, std::strerror(errno)));
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, stdoutPipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, stderrPipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdoutPipe[0]);
        posix_spawn_file_actions_addclose(&actions, stderrPipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdoutPipe[1]);
        posix_spawn_file_actions_addclose(&actions, stderrPipe[1]);
#if defined(__APPLE__) || defined(__GLIBC__)
        if (!request.workingDirectory.empty())
            posix_spawn_file_actions_addchdir_np(&actions, request.workingDirectory.c_str());
#else
        if (!request.workingDirectory.empty()) {
            posix_spawn_file_actions_destroy(&actions);
            close(stdoutPipe[0]);
            close(stdoutPipe[1]);
            close(stderrPipe[0]);
            close(stderrPipe[1]);
            return Result<ExternalProcessResult>::Failure(
                MakeError(PlatformErrors::ProcessLaunchFailed, "Working directories are unavailable on this POSIX host."));
        }
#endif

        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes, 0);

        std::vector<std::string> argumentStorage;
        argumentStorage.reserve(request.arguments.size() + 1U);
        argumentStorage.push_back(request.executable);
        argumentStorage.insert(argumentStorage.end(), request.arguments.begin(), request.arguments.end());
        std::vector<char *> arguments;
        for (std::string &argument : argumentStorage)
            arguments.push_back(argument.data());
        arguments.push_back(nullptr);

        std::vector<std::string> environmentStorage = BuildEnvironment(request.environment);
        std::vector<char *> environment;
        for (std::string &entry : environmentStorage)
            environment.push_back(entry.data());
        environment.push_back(nullptr);

        pid_t process{};
        const int spawned = posix_spawnp(&process, request.executable.c_str(), &actions, &attributes, arguments.data(), environment.data());
        posix_spawnattr_destroy(&attributes);
        posix_spawn_file_actions_destroy(&actions);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);
        if (spawned != 0) {
            close(stdoutPipe[0]);
            close(stderrPipe[0]);
            return Result<ExternalProcessResult>::Failure(MakeError(PlatformErrors::ProcessLaunchFailed, std::strerror(spawned)));
        }
        static_cast<void>(fcntl(stdoutPipe[0], F_SETFL, fcntl(stdoutPipe[0], F_GETFL) | O_NONBLOCK));
        static_cast<void>(fcntl(stderrPipe[0], F_SETFL, fcntl(stderrPipe[0], F_GETFL) | O_NONBLOCK));

        LineDecoder standardOutput{ProcessOutputStream::StandardOutput, request.maximumLineBytes, request.onOutput};
        LineDecoder standardError{ProcessOutputStream::StandardError, request.maximumLineBytes, request.onOutput};
        bool stdoutOpen = true;
        bool stderrOpen = true;
        bool childExited = false;
        bool terminationRequested = false;
        bool timedOut = false;
        int status{};
        const auto started = std::chrono::steady_clock::now();
        auto terminationStarted = started;

        while (!childExited || stdoutOpen || stderrOpen) {
            const auto now = std::chrono::steady_clock::now();
            const bool cancelled = cancellation.IsCancellationRequested();
            if (!terminationRequested && (cancelled || now - started >= request.timeout)) {
                terminationRequested = true;
                timedOut = !cancelled;
                terminationStarted = now;
                static_cast<void>(kill(-process, SIGTERM));
            } else if (terminationRequested && !childExited && now - terminationStarted >= request.gracefulTermination) {
                static_cast<void>(kill(-process, SIGKILL));
            }

            std::array<pollfd, 2> descriptors{{{stdoutPipe[0], static_cast<short>(stdoutOpen ? POLLIN : 0), 0},
                                                {stderrPipe[0], static_cast<short>(stderrOpen ? POLLIN : 0), 0}}};
            static_cast<void>(poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()), 25));
            if (stdoutOpen && (descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
                Drain(stdoutPipe[0], stdoutOpen, standardOutput);
            if (stderrOpen && (descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
                Drain(stderrPipe[0], stderrOpen, standardError);
            if (!childExited)
                childExited = waitpid(process, &status, WNOHANG) == process;
        }
        close(stdoutPipe[0]);
        close(stderrPipe[0]);

        ExternalProcessResult result;
        if (timedOut)
            result.reason = ProcessTerminationReason::TimedOut;
        else if (terminationRequested)
            result.reason = ProcessTerminationReason::Cancelled;
        else if (WIFSIGNALED(status))
            result.reason = ProcessTerminationReason::Signalled;
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        return Result<ExternalProcessResult>::Success(result);
    }
}  // namespace Horo
