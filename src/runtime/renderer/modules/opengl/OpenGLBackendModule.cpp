#include "OpenGLBackendInternal.h"

namespace Horo::Render {
    /** @copydoc GetOpenGLRenderBackendModuleInfo */
    const RenderBackendModuleInfo &GetOpenGLRenderBackendModuleInfo() noexcept {
        static const RenderBackendModuleInfo info{
            .id = RenderBackendId{"opengl"},
            .displayName = "OpenGL",
            .windowRequirements =
                RenderHostWindowRequirements{
                    .presentation = RenderPresentationKind::OpenGL,
                    .resizable = true,
                    .highPixelDensity = true,
                },
            .supportsInteractivePresentation = true,
        };
        return info;
    }

    /** @copydoc RegisterOpenGLRenderBackend */
    Result<void> RegisterOpenGLRenderBackend(RenderBackendRegistry &registry, IOpenGLPresentationPort &presentationPort,
                                             const OpenGLBackendOptions options) {
        return Detail::RegisterOpenGLRenderBackendWithFunctions(registry, presentationPort, options,
                                                                Detail::ProductionOpenGLCommandFunctions());
    }
}  // namespace Horo::Render
