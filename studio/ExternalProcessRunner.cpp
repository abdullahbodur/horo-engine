#include "ExternalProcessRunner.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Logger.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Monolith::Launcher {

namespace {

void LogProcessLine(const std::string& label, const std::string& line) {
  if (line.empty())
    return;
  LOG_INFO("[%s] %s", label.c_str(), line.c_str());
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty())
    return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (length <= 0)
    return {};
  std::wstring out(static_cast<size_t>(length - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), length);
  return out;
}

std::wstring BuildCommandLine(const ResolvedLauncherCommand& command) {
  auto quote = [](const std::string& part) {
    if (part.find(' ') == std::string::npos &&
        part.find('\t') == std::string::npos &&
        part.find('"') == std::string::npos) {
      return part;
    }
    std::string quoted = "\"";
    for (char c : part) {
      if (c == '"')
        quoted += "\\\"";
      else
        quoted.push_back(c);
    }
    quoted += '"';
    return quoted;
  };

  std::string commandLine = quote(command.executable.generic_string());
  for (const std::string& arg : command.args) {
    commandLine.push_back(' ');
    commandLine += quote(arg);
  }
  return Utf8ToWide(commandLine);
}
#endif

#ifndef _WIN32
int DecodePosixExitCode(int status, int fallback = 1) {
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return fallback;
}

bool TryWaitForPid(pid_t pid, int* status, int options, bool* outReaped) {
  if (!status || !outReaped)
    return false;
  const pid_t result = waitpid(pid, status, options);
  if (result == pid) {
    *outReaped = true;
    return true;
  }
  if (result < 0)
    return false;
  *outReaped = false;
  return true;
}

int StopPosixProcess(pid_t* pid) {
  if (!pid || *pid <= 0)
    return 1;

  int status = 0;
  bool reaped = false;
  kill(*pid, SIGTERM);
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (!TryWaitForPid(*pid, &status, WNOHANG, &reaped))
      break;
    if (reaped)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  if (!reaped) {
    kill(*pid, SIGKILL);
    const pid_t result = waitpid(*pid, &status, 0);
    reaped = result == *pid;
  }
  *pid = -1;
  return reaped ? DecodePosixExitCode(status) : 1;
}
#endif

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
using ReaderThread = std::jthread;
#else
using ReaderThread = std::thread; // NOSONAR: C++17 fallback when std::jthread is unavailable
#endif

}  // namespace

struct ExternalProcessRunner::ProcessHandle {
  ReaderThread readerThread;
  std::mutex readerMutex;
  std::atomic<bool> readerDone{false};
#ifdef _WIN32
  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  HANDLE stdoutRead = nullptr;
  HANDLE stdoutWrite = nullptr;
#else
  pid_t pid = -1;
  int stdoutRead = -1;
  int stdoutWrite = -1;
#endif
};

ExternalProcessRunner::ExternalProcessRunner() = default;

ExternalProcessRunner::~ExternalProcessRunner() {
  Stop();
}

bool ExternalProcessRunner::Start(const ResolvedLauncherCommand& command,  // NOSONAR: function orchestrates platform-specific process startup
                                  const std::string& label,
                                  std::string* outError) {
  if (outError)
    outError->clear();
  if (m_process && !m_status.active)
    Stop();
  if (IsActive()) {
    if (outError)
      *outError = "Another external process is already running.";
    return false;
  }

  m_status = {};
  m_status.label = label;
  m_status.commandLine = command.debugString;

  auto process = std::make_unique<ProcessHandle>();

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;
  if (!CreatePipe(&process->stdoutRead, &process->stdoutWrite, &sa, 0)) {
    if (outError)
      *outError = "CreatePipe failed.";
    return false;
  }
  SetHandleInformation(process->stdoutRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = process->stdoutWrite;
  startup.hStdError = process->stdoutWrite;

  PROCESS_INFORMATION info{};
  std::wstring commandLine = BuildCommandLine(command);
  std::wstring workdir = Utf8ToWide(command.workingDirectory.generic_string());
  if (const BOOL created = CreateProcessW(nullptr,
                                       commandLine.data(),
                                       nullptr,
                                       nullptr,
                                       TRUE,
                                       CREATE_NO_WINDOW,
                                       nullptr,
                                       workdir.empty() ? nullptr : workdir.c_str(),
                                       &startup,
                                       &info); !created) {
    CloseHandle(process->stdoutRead);
    CloseHandle(process->stdoutWrite);
    process->stdoutRead = nullptr;
    process->stdoutWrite = nullptr;
    if (outError)
      *outError = "CreateProcessW failed.";
    return false;
  }

  CloseHandle(process->stdoutWrite);
  process->stdoutWrite = nullptr;
  process->process = info.hProcess;
  process->thread = info.hThread;

  process->readerThread = ReaderThread([handle = process.get(), label]() {
    std::array<char, 512> buffer{};
    std::string pending;
    DWORD bytesRead = 0;
    while (ReadFile(handle->stdoutRead, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
           bytesRead > 0) {
      pending.append(buffer.data(), bytesRead);
      size_t newlinePos = 0;
      while ((newlinePos = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, newlinePos);
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        LogProcessLine(label, line);
        pending.erase(0, newlinePos + 1);
      }
    }
    if (!pending.empty())
      LogProcessLine(label, pending);
    handle->readerDone = true;
  });
#else
  std::array<int, 2> pipes{-1, -1};
  if (pipe(pipes.data()) != 0) {
    if (outError)
      *outError = "pipe() failed.";
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipes[0]);
    close(pipes[1]);
    if (outError)
      *outError = "fork() failed.";
    return false;
  }

  if (pid == 0) {
    if (pipes[1] < 0 || dup2(pipes[1], STDOUT_FILENO) < 0 || dup2(pipes[1], STDERR_FILENO) < 0)
      _exit(127);
    close(pipes[0]);
    close(pipes[1]);
    if (!command.workingDirectory.empty() &&
        chdir(command.workingDirectory.string().c_str()) != 0) {
      _exit(126);
    }

    std::vector<std::string> ownedArgs;
    ownedArgs.reserve(command.args.size() + 1);
    ownedArgs.push_back(command.executable.generic_string());
    for (const std::string& arg : command.args)
      ownedArgs.push_back(arg);

    std::vector<char*> argv;
    argv.reserve(ownedArgs.size() + 1);
    for (std::string& arg : ownedArgs)
      argv.push_back(arg.data());
    argv.push_back(nullptr);

    execvp(command.executable.generic_string().c_str(), argv.data());
    _exit(127);
  }

  close(pipes[1]);
  process->pid = pid;
  process->stdoutRead = pipes[0];
  process->readerThread = ReaderThread([handle = process.get(), label]() {
    std::array<char, 512> buffer{};
    std::string pending;
    ssize_t bytesRead = 0;
    while ((bytesRead = read(handle->stdoutRead, buffer.data(), buffer.size())) > 0) {
      pending.append(buffer.data(), static_cast<size_t>(bytesRead));
      size_t newlinePos = 0;
      while ((newlinePos = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, newlinePos);
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        LogProcessLine(label, line);
        pending.erase(0, newlinePos + 1);
      }
    }
    if (!pending.empty())
      LogProcessLine(label, pending);
    handle->readerDone = true;
  });
#endif

  m_status.active = true;
  m_process = std::move(process);
  LOG_INFO("[%s] started: %s", label.c_str(), command.debugString.c_str());
  return true;
}

void ExternalProcessRunner::Poll() {
  if (!m_process || !m_status.active)
    return;

#ifdef _WIN32
  if (const DWORD waitResult = WaitForSingleObject(m_process->process, 0); waitResult != WAIT_OBJECT_0)
    return;

  DWORD exitCode = 0;
  GetExitCodeProcess(m_process->process, &exitCode);
  Finish(static_cast<int>(exitCode), false);
#else
  int status = 0;
  if (const pid_t result = waitpid(m_process->pid, &status, WNOHANG); result <= 0)
    return;

  int exitCode = 0;
  if (WIFEXITED(status))
    exitCode = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    exitCode = 128 + WTERMSIG(status);
  Finish(exitCode, false);
#endif
}

void ExternalProcessRunner::Stop() {
  if (!m_process)
    return;

  if (m_status.active) {
#ifdef _WIN32
    int exitCode = 1;
    if (m_process->process)
      TerminateProcess(m_process->process, 1);
    if (m_process->process) {
      const DWORD waitResult = WaitForSingleObject(m_process->process, 5000);
      if (waitResult == WAIT_OBJECT_0) {
        DWORD nativeExitCode = 1;
        if (GetExitCodeProcess(m_process->process, &nativeExitCode))
          exitCode = static_cast<int>(nativeExitCode);
      }
    }
#else
    const int exitCode = StopPosixProcess(&m_process->pid);
#endif
    Finish(exitCode, true);
  }

  if (m_process->readerThread.joinable())
    m_process->readerThread.join();

#ifdef _WIN32
  if (m_process->stdoutRead)
    CloseHandle(m_process->stdoutRead);
  if (m_process->stdoutWrite)
    CloseHandle(m_process->stdoutWrite);
  if (m_process->thread)
    CloseHandle(m_process->thread);
  if (m_process->process)
    CloseHandle(m_process->process);
#else
  if (m_process->stdoutRead >= 0)
    close(m_process->stdoutRead);
  if (m_process->stdoutWrite >= 0)
    close(m_process->stdoutWrite);
#endif

  m_process.reset();
}

bool ExternalProcessRunner::IsActive() const {
  return m_status.active;
}

void ExternalProcessRunner::Finish(int exitCode, bool terminatedByUser, std::string error) {
  m_status.active = false;
  m_status.finished = true;
  m_status.terminatedByUser = terminatedByUser;
  m_status.exitCode = exitCode;
  m_status.error = std::move(error);

  if (!m_status.error.empty())
    LOG_ERROR("[%s] failed: %s", m_status.label.c_str(), m_status.error.c_str());
  else if (m_status.terminatedByUser)
    LOG_WARN("[%s] terminated by user", m_status.label.c_str());
  else
    LOG_INFO("[%s] exited with code %d", m_status.label.c_str(), m_status.exitCode);
}

}  // namespace Monolith::Launcher
