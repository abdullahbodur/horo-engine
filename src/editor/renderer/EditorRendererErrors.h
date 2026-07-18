#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Editor::RendererErrors {
    inline const ErrorDomainId GuiOpenGLDomain{"horo.editor.gui.opengl"};
    inline const ErrorDomainId ViewportOpenGLDomain{"horo.editor.viewport.opengl"};
    inline const ErrorDomainId SdlOpenGLDomain{"horo.editor.sdl.opengl"};
    inline const ErrorDomainId ViewportDomain{"horo.editor.viewport"};

    inline const ErrorCodeDescriptor GuiInvalidState{
        GuiOpenGLDomain, ErrorCode{"editor.gui.opengl.invalid_state"}, ErrorSeverity::Error,
        "OpenGL GUI renderer state is invalid.", {}, false, false
    };
    inline const ErrorCodeDescriptor GuiInitializationFailed{
        GuiOpenGLDomain, ErrorCode{"editor.gui.opengl.initialization_failed"}, ErrorSeverity::Error,
        "OpenGL GUI renderer initialization failed.", {}, true, false
    };
    inline const ErrorCodeDescriptor GuiNotInitialized{
        GuiOpenGLDomain, ErrorCode{"editor.gui.opengl.not_initialized"}, ErrorSeverity::Error,
        "OpenGL GUI renderer is not initialized.", {}, false, false
    };
    inline const ErrorCodeDescriptor GuiInvalidTexture{
        GuiOpenGLDomain, ErrorCode{"editor.gui.opengl.invalid_texture"}, ErrorSeverity::Error,
        "OpenGL GUI texture upload is invalid.", {}, false, false
    };
    inline const ErrorCodeDescriptor GuiTextureCreationFailed{
        GuiOpenGLDomain, ErrorCode{"editor.gui.opengl.texture_creation_failed"}, ErrorSeverity::Error,
        "OpenGL GUI texture creation failed.", {}, true, false
    };

    inline const ErrorCodeDescriptor ViewportShaderCompileFailed{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.shader_compile_failed"}, ErrorSeverity::Error,
        "Viewport shader compilation failed.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportAlreadyInitialized{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.already_initialized"}, ErrorSeverity::Error,
        "Viewport renderer is already initialized.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportOpenGLDispatchFailed{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.opengl_dispatch_failed"}, ErrorSeverity::Error,
        "OpenGL entry-point loading failed.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportNotInitialized{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.not_initialized"}, ErrorSeverity::Error,
        "Viewport renderer is not initialized.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportInvalidScene{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.invalid_scene"}, ErrorSeverity::Error,
        "Viewport scene data is invalid.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportStaleTarget{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.stale_target"}, ErrorSeverity::Error,
        "Viewport pass references a stale target.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportStaleMeshResource{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.stale_mesh_resource"}, ErrorSeverity::Error,
        "Viewport instance references a stale mesh resource.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportShaderLinkFailed{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.shader_link_failed"}, ErrorSeverity::Error,
        "Viewport shader linking failed.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportShaderContractInvalid{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.shader_contract_invalid"}, ErrorSeverity::Error,
        "Viewport shader contract is invalid.", {}, false, false
    };
    inline const ErrorCodeDescriptor ViewportGeometryCreationFailed{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.geometry_creation_failed"}, ErrorSeverity::Error,
        "Viewport geometry creation failed.", {}, true, false
    };
    inline const ErrorCodeDescriptor ViewportFramebufferIncomplete{
        ViewportOpenGLDomain, ErrorCode{"editor.viewport.framebuffer_incomplete"}, ErrorSeverity::Error,
        "OpenGL viewport framebuffer is incomplete.", {}, true, false
    };

    inline const ErrorCodeDescriptor SdlContextExists{
        SdlOpenGLDomain, ErrorCode{"render.sdl.context_exists"}, ErrorSeverity::Error,
        "An SDL OpenGL context is already retained.", {}, false, false
    };
    inline const ErrorCodeDescriptor SdlContextAttributesFailed{
        SdlOpenGLDomain, ErrorCode{"render.sdl.context_attributes_failed"}, ErrorSeverity::Error,
        "SDL OpenGL context attribute setup failed.", {}, true, false
    };
    inline const ErrorCodeDescriptor SdlContextCreationFailed{
        SdlOpenGLDomain, ErrorCode{"render.sdl.context_creation_failed"}, ErrorSeverity::Error,
        "SDL OpenGL context creation failed.", {}, true, false
    };
    inline const ErrorCodeDescriptor SdlMakeCurrentFailed{
        SdlOpenGLDomain, ErrorCode{"render.sdl.make_current_failed"}, ErrorSeverity::Error,
        "SDL OpenGL context activation failed.", {}, true, false
    };
    inline const ErrorCodeDescriptor SdlInvalidPresentMode{
        SdlOpenGLDomain, ErrorCode{"render.sdl.invalid_present_mode"}, ErrorSeverity::Error,
        "SDL OpenGL presentation mode is invalid.", {}, false, false
    };
    inline const ErrorCodeDescriptor SdlPresentModeFailed{
        SdlOpenGLDomain, ErrorCode{"render.sdl.present_mode_failed"}, ErrorSeverity::Error,
        "SDL OpenGL presentation mode setup failed.", {}, true, false
    };
    inline const ErrorCodeDescriptor SdlSwapFailed{
        SdlOpenGLDomain, ErrorCode{"render.sdl.swap_failed"}, ErrorSeverity::Error,
        "SDL OpenGL buffer swap failed.", {}, true, false
    };

    inline const ErrorCodeDescriptor InvalidRenderCamera{
        ViewportDomain, ErrorCode{"editor.viewport.invalid_render_camera"}, ErrorSeverity::Error,
        "Render camera, transform, or aspect ratio is invalid.", {}, false, false
    };
    inline const ErrorCodeDescriptor InvalidCamera{
        ViewportDomain, ErrorCode{"editor.viewport.invalid_camera"}, ErrorSeverity::Error,
        "Viewport camera or aspect ratio is invalid.", {}, false, false
    };
    inline const ErrorCodeDescriptor InvalidCoordinates{
        ViewportDomain, ErrorCode{"editor.viewport.invalid_coordinates"}, ErrorSeverity::Error,
        "Viewport coordinates are outside normalized bounds.", {}, false, false
    };
} // namespace Horo::Editor::RendererErrors
