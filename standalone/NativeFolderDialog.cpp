#include "standalone/NativeFolderDialog.h"

#include <string>

#ifdef _WIN32
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <algorithm>
#include <cstdio>
#endif

namespace Monolith::Standalone {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty())
    return {};
  const int required =
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
  if (required <= 0)
    return {};

  std::wstring wide(static_cast<size_t>(required), L'\0');
  const int converted = MultiByteToWideChar(
      CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), required);
  if (converted <= 0)
    return {};
  return wide;
}

class ScopedComInit {
 public:
  ScopedComInit() : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}

  ~ScopedComInit() {
    if (SUCCEEDED(m_result))
      CoUninitialize();
  }

  bool Ready() const { return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE; }

 private:
  HRESULT m_result;
};
#elif defined(__APPLE__)
std::string EscapeAppleScriptString(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    if (c == '\\' || c == '"')
      escaped.push_back('\\');
    escaped.push_back(c);
  }
  return escaped;
}

std::string ReadPathFromOsascript(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe)
    return {};

  char buffer[1024] = {};
  std::string output;
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
    output += buffer;
  pclose(pipe);

  output.erase(std::remove_if(output.begin(),
                              output.end(),
                              [](const char c) { return c == '\n' || c == '\r'; }),
               output.end());
  return output;
}
#endif

}  // namespace

fs::path PickFolderPath(const char* prompt, const fs::path& defaultPath) {
#ifdef _WIN32
  ScopedComInit comInit;
  if (!comInit.Ready())
    return {};

  IFileOpenDialog* dialog = nullptr;
  const HRESULT createResult =
      CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(createResult) || !dialog)
    return {};

  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       FOS_NOCHANGEDIR);
  }

  if (prompt && *prompt) {
    const std::wstring title = Utf8ToWide(prompt);
    if (!title.empty())
      dialog->SetTitle(title.c_str());
  }

  std::error_code ec;
  const fs::path initialPath =
      defaultPath.empty() ? fs::path() : fs::weakly_canonical(defaultPath, ec);
  if (!ec && !initialPath.empty() && fs::is_directory(initialPath)) {
    IShellItem* initialFolder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initialPath.c_str(), nullptr, IID_PPV_ARGS(&initialFolder))) &&
        initialFolder) {
      dialog->SetFolder(initialFolder);
      initialFolder->Release();
    }
  }

  fs::path resultPath;
  if (SUCCEEDED(dialog->Show(nullptr))) {
    IShellItem* result = nullptr;
    if (SUCCEEDED(dialog->GetResult(&result)) && result) {
      PWSTR selectedPath = nullptr;
      if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath)) && selectedPath) {
        resultPath = fs::path(selectedPath);
        CoTaskMemFree(selectedPath);
      }
      result->Release();
    }
  }

  dialog->Release();
  return resultPath;
#elif defined(__APPLE__)
  std::string command =
      "/usr/bin/osascript -e 'try' "
      "-e 'POSIX path of (choose folder with prompt \"" +
      EscapeAppleScriptString(prompt ? std::string(prompt) : std::string("Select folder")) +
      "\")' "
      "-e 'on error' -e 'return \"\"' -e 'end try' 2>/dev/null";
  return fs::path(ReadPathFromOsascript(command));
#else
  (void)prompt;
  (void)defaultPath;
  return {};
#endif
}

}  // namespace Monolith::Standalone
