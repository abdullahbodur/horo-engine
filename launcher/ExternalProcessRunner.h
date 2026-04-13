#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "launcher/LauncherProject.h"

namespace Monolith::Launcher {

struct ExternalProcessStatus {
  bool active = false;
  bool finished = false;
  bool failedToStart = false;
  bool terminatedByUser = false;
  int exitCode = 0;
  std::string label;
  std::string commandLine;
  std::string error;
};

class ExternalProcessRunner {
 public:
  ExternalProcessRunner();
  ~ExternalProcessRunner();

  ExternalProcessRunner(const ExternalProcessRunner&) = delete;
  ExternalProcessRunner& operator=(const ExternalProcessRunner&) = delete;

  bool Start(const ResolvedLauncherCommand& command,
             const std::string& label,
             std::string* outError);
  void Poll();
  void Stop();

  bool IsActive() const;
  const ExternalProcessStatus& GetStatus() const { return m_status; }

 private:
  struct ProcessHandle;
  std::unique_ptr<ProcessHandle> m_process;
  ExternalProcessStatus m_status;

  void Finish(int exitCode, bool terminatedByUser, std::string error = {});
};

}  // namespace Monolith::Launcher
