#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Platform/DynamicLibrary.h"
#include "Horo/Platform/PlatformErrors.h"

#include <string>
#include <windows.h>

namespace Horo::Platform {
    namespace {
        class WindowsDynamicLibrary final : public DynamicLibrary {
        public:
            explicit WindowsDynamicLibrary(HMODULE handle) : m_handle(handle) {}

            ~WindowsDynamicLibrary() override {
                if (m_handle) {
                    FreeLibrary(m_handle);
                }
            }

            [[nodiscard]] void *GetSymbol(std::string_view name) const noexcept override {
                std::string symbolName{name};
                return reinterpret_cast<void *>(GetProcAddress(m_handle, symbolName.c_str()));
            }

        private:
            HMODULE m_handle = nullptr;
        };
    }  // namespace

    Result<std::unique_ptr<DynamicLibrary>> LoadDynamicLibrary(std::string_view path) {
        std::string libPath{path};
        HMODULE handle = LoadLibraryA(libPath.c_str());
        if (!handle) {
            DWORD error = GetLastError();
            std::string errorMsg = "LoadLibrary failed with error code: " + std::to_string(error);
            LOG_ERROR("platform.dynamic_library", "Failed to load dynamic library %s: %s", libPath.c_str(), errorMsg.c_str());
            return Result<std::unique_ptr<DynamicLibrary>>::Failure(MakeError(PlatformErrors::InvalidFormat, errorMsg));
        }

        return Result<std::unique_ptr<DynamicLibrary>>::Success(std::make_unique<WindowsDynamicLibrary>(handle));
    }
}  // namespace Horo::Platform
