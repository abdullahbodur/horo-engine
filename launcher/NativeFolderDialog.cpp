#include "launcher/NativeFolderDialog.h"

#include <string>

#ifdef _WIN32
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <algorithm>
#include <cstdio>
#endif

namespace Horo::Launcher {
    namespace fs = std::filesystem;

#if defined(_WIN32) || defined(__APPLE__)
    namespace {
#ifdef _WIN32
        std::wstring Utf8ToWide(const std::string &value) {
            if (value.empty())
                return {};
            const int required = MultiByteToWideChar(
                CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
            if (required <= 0)
                return {};

            std::wstring wide(static_cast<size_t>(required), L'\0');
            if (const int converted = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                                          static_cast<int>(value.size()),
                                                          wide.data(), required);
                converted <= 0) {
                return {};
            }
            return wide;
        }

        class ScopedComInit {
        public:
            ScopedComInit() = default;

            ~ScopedComInit() {
                if (SUCCEEDED(m_result))
                    CoUninitialize();
            }

            ScopedComInit(const ScopedComInit &) = delete;

            ScopedComInit &operator=(const ScopedComInit &) = delete;

            ScopedComInit(ScopedComInit &&) = delete;

            ScopedComInit &operator=(ScopedComInit &&) = delete;

            bool Ready() const {
                return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
            }

        private:
            HRESULT m_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                       COINIT_DISABLE_OLE1DDE);
        };
#elif defined(__APPLE__)
        std::string EscapeAppleScriptString(const std::string &value) {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char c: value) {
                if (c == '\\' || c == '"')
                    escaped.push_back('\\');
                escaped.push_back(c);
            }
            return escaped;
        }

        std::string ReadPathFromOsascript(const std::string &command) {
            FILE *pipe = popen(command.c_str(), "r");
            if (!pipe)
                return {};

            std::string buffer(1024, '\0');
            std::string output;
            while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
                   nullptr)
                output += buffer.data();
            pclose(pipe);

            std::erase_if(output, [](const char c) { return c == '\n' || c == '\r'; });
            return output;
        }
#endif
    } // namespace
#endif

    fs::path PickFolderPath(const char *prompt,
                            [[maybe_unused]] const fs::path &defaultPath) {
#ifdef _WIN32
        if (ScopedComInit comInit; !comInit.Ready())
            return {};

        IFileOpenDialog *dialog = nullptr;
        if (const HRESULT createResult =
                    CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&dialog));
            FAILED(createResult) || !dialog) {
            return {};
        }

        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                               FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        }

        if (prompt && *prompt) {
            const std::wstring title = Utf8ToWide(prompt);
            if (!title.empty())
                dialog->SetTitle(title.c_str());
        }

        std::error_code ec;
        if (const fs::path initialPath = defaultPath.empty()
                                             ? fs::path()
                                             : fs::weakly_canonical(defaultPath, ec);
            !ec && !initialPath.empty() && fs::is_directory(initialPath)) {
            IShellItem *initialFolder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialPath.c_str(), nullptr,
                IID_PPV_ARGS(&initialFolder))) &&
                initialFolder) {
                dialog->SetFolder(initialFolder);
                initialFolder->Release();
            }
        }

        fs::path resultPath;
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem *result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)) && result) {
                if (PWSTR selectedPath = nullptr;
                    SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath)) &&
                    selectedPath) {
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
                EscapeAppleScriptString(prompt
                                            ? std::string(prompt)
                                            : std::string("Select folder")) +
                R"(")' )"
                R"(-e 'on error' -e 'return ""' -e 'end try' 2>/dev/null)";
        return fs::path(ReadPathFromOsascript(command));
#else
        (void) prompt;
        (void) defaultPath;
        return {};
#endif
    }
} // namespace Horo::Launcher
