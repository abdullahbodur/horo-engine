#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Platform/DynamicLibrary.h"
#include "Horo/Platform/PlatformErrors.h"

#include <dlfcn.h>
#include <string>

namespace Horo::Platform {
    namespace {
        class PosixDynamicLibrary final : public DynamicLibrary {
        public:
            explicit PosixDynamicLibrary(void *handle) : m_handle(handle) {}

            ~PosixDynamicLibrary() override {
                if (m_handle) {
                    dlclose(m_handle);
                }
            }

            [[nodiscard]] void *GetSymbol(std::string_view name) const noexcept override {
                // dlsym requires null-terminated string
                std::string symbolName{name};
                return dlsym(m_handle, symbolName.c_str());
            }

        private:
            void *m_handle = nullptr;
        };
    }  // namespace

    [[nodiscard]] bool IsSafeLibraryPath(const std::string_view path) noexcept {
        if (path.empty() || path.size() > 1024)
            return false;
        if (path.find("..") != std::string_view::npos)
            return false;
        return true;
    }

    Result<std::unique_ptr<DynamicLibrary>> LoadDynamicLibrary(std::string_view path) {
        if (!IsSafeLibraryPath(path))
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(
                MakeError(PlatformErrors::InvalidFormat, "Library path is invalid or contains traversal"));
        if (path.find("..") != std::string_view::npos || path.empty())
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(
                MakeError(PlatformErrors::InvalidFormat, "Library path contains traversal"));
        std::string libPath{path};
        if (libPath.find("..") != std::string::npos || libPath.empty())
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(
                MakeError(PlatformErrors::InvalidFormat, "Library path contains traversal"));
        void *handle = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char *nativeError = dlerror();
            std::string errorMsg = nativeError != nullptr ? nativeError : "Unknown dlopen error";
            LOG_ERROR("platform.dynamic_library", "Failed to load dynamic library %s: %s", libPath.c_str(), errorMsg.c_str());
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(MakeError(PlatformErrors::InvalidFormat, errorMsg));
        }

        return Result<std::unique_ptr<DynamicLibrary>>::Success(std::make_unique<PosixDynamicLibrary>(handle));
    }
}  // namespace Horo::Platform
