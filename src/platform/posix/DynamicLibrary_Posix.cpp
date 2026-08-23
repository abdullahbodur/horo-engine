#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Platform/DynamicLibrary.h"
#include "Horo/Platform/PlatformErrors.h"

#include <algorithm>
#include <dlfcn.h>
#include <filesystem>
#include <string>

namespace Horo::Platform {
    namespace {
        class PosixDynamicLibrary final : public DynamicLibrary {
        public:
            explicit PosixDynamicLibrary(void *handle) : m_handle(handle) {}  // NOSONAR(cpp:S5008)

            ~PosixDynamicLibrary() override {
                if (m_handle) {
                    dlclose(m_handle);
                }
            }

            PosixDynamicLibrary(const PosixDynamicLibrary &) = delete;
            PosixDynamicLibrary &operator=(const PosixDynamicLibrary &) = delete;
            PosixDynamicLibrary(PosixDynamicLibrary &&) = delete;
            PosixDynamicLibrary &operator=(PosixDynamicLibrary &&) = delete;

            [[nodiscard]] void *GetSymbol(std::string_view name) const noexcept override {  // NOSONAR(cpp:S5008)
                // dlsym requires null-terminated string
                std::string symbolName{name};
                return dlsym(m_handle, symbolName.c_str());
            }

        private:
            void *m_handle = nullptr;  // NOSONAR(cpp:S5008)
        };
    }  // namespace

    [[nodiscard]] bool IsSafeLibraryPath(const std::string_view path) noexcept {
        if (path.empty() || path.size() > 1024)
            return false;
        const std::filesystem::path candidate{path};
        return candidate.is_absolute() && std::ranges::none_of(candidate, [](const std::filesystem::path &part) {
            return part == "..";
        });
    }

    Result<std::unique_ptr<DynamicLibrary>> LoadDynamicLibrary(std::string_view path) {
        if (!IsSafeLibraryPath(path))
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(
                MakeError(PlatformErrors::InvalidFormat, "Library path is invalid or contains traversal"));
        std::error_code error;
        const std::filesystem::path canonicalPath = std::filesystem::canonical(std::filesystem::path{path}, error);
        if (error || !canonicalPath.is_absolute() || !std::filesystem::is_regular_file(canonicalPath, error))
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(
                MakeError(PlatformErrors::InvalidFormat, "Library path does not resolve to a regular file"));
        const std::string libPath = canonicalPath.string();
        // Loading project-produced native code is an explicit host capability; the path is canonical and absolute.
        void *handle = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);  // NOSONAR
        if (!handle) {
            const char *nativeError = dlerror();
            std::string errorMsg = nativeError != nullptr ? nativeError : "Unknown dlopen error";
            LOG_ERROR("platform.dynamic_library", "Failed to load the validated dynamic library: %s", errorMsg.c_str());
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(MakeError(PlatformErrors::InvalidFormat, errorMsg));
        }

        return Result<std::unique_ptr<DynamicLibrary>>::Success(std::make_unique<PosixDynamicLibrary>(handle));
    }
}  // namespace Horo::Platform
