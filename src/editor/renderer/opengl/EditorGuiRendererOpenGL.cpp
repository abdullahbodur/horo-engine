#include "EditorGuiRendererOpenGL.h"
#include "editor/renderer/EditorRendererErrors.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <algorithm>
#include <limits>
#include <string>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] Error MakeGuiRendererError(const ErrorCodeDescriptor &descriptor, std::string message)
{
    return MakeError(descriptor, std::move(message));
}
} // namespace

/** @copydoc EditorGuiRendererOpenGL::EditorGuiRendererOpenGL */
EditorGuiRendererOpenGL::EditorGuiRendererOpenGL(SDL_Window &window, const SDL_GLContext context) noexcept
    : window_(&window), context_(context)
{
}

/** @copydoc EditorGuiRendererOpenGL::~EditorGuiRendererOpenGL */
EditorGuiRendererOpenGL::~EditorGuiRendererOpenGL()
{
    Shutdown();
}

/** @copydoc EditorGuiRendererOpenGL::Initialize */
Result<void> EditorGuiRendererOpenGL::Initialize()
{
    if (platformInitialized_ || rendererInitialized_ || context_ == nullptr)
    {
        return Result<void>::Failure(MakeGuiRendererError(RendererErrors::GuiInvalidState,
                                                          "OpenGL GUI renderer state or context is invalid."));
    }
    platformInitialized_ = ImGui_ImplSDL3_InitForOpenGL(window_, context_);
    rendererInitialized_ = platformInitialized_ && ImGui_ImplOpenGL3_Init("#version 150");
    if (!rendererInitialized_)
    {
        Shutdown();
        return Result<void>::Failure(MakeGuiRendererError(RendererErrors::GuiInitializationFailed,
                                                          "Failed to initialize Dear ImGui SDL3/OpenGL bridges."));
    }
    return Result<void>::Success();
}

/** @copydoc EditorGuiRendererOpenGL::BeginFrame */
Result<void> EditorGuiRendererOpenGL::BeginFrame()
{
    if (!rendererInitialized_)
    {
        return Result<void>::Failure(
            MakeGuiRendererError(RendererErrors::GuiNotInitialized, "OpenGL GUI renderer is not initialized."));
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    return Result<void>::Success();
}

/** @copydoc EditorGuiRendererOpenGL::RenderDrawData */
Result<void> EditorGuiRendererOpenGL::RenderDrawData()
{
    if (!rendererInitialized_)
    {
        return Result<void>::Failure(
            MakeGuiRendererError(RendererErrors::GuiNotInitialized, "OpenGL GUI renderer is not initialized."));
    }
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return Result<void>::Success();
}

/** @copydoc EditorGuiRendererOpenGL::CreateTexture */
Result<std::uintptr_t> EditorGuiRendererOpenGL::CreateTexture(const EditorRgba8ImageView &image)
{
    if (!rendererInitialized_ || !image.IsValid() ||
        image.width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
        image.height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
    {
        return Result<std::uintptr_t>::Failure(
            MakeGuiRendererError(RendererErrors::GuiInvalidTexture, "OpenGL GUI texture upload is invalid."));
    }
    GLuint texture = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    if (texture == 0)
    {
        return Result<std::uintptr_t>::Failure(
            MakeGuiRendererError(RendererErrors::GuiTextureCreationFailed, "Failed to create OpenGL GUI texture."));
    }
    textures_.push_back(texture);
    return Result<std::uintptr_t>::Success(static_cast<std::uintptr_t>(texture));
}

/** @copydoc EditorGuiRendererOpenGL::DestroyTexture */
void EditorGuiRendererOpenGL::DestroyTexture(const std::uintptr_t textureId) noexcept
{
    const auto found = std::ranges::find(textures_, static_cast<std::uint32_t>(textureId));
    if (found != textures_.end())
    {
        const GLuint texture = *found;
        glDeleteTextures(1, &texture);
        textures_.erase(found);
    }
}

/** @copydoc EditorGuiRendererOpenGL::Shutdown */
void EditorGuiRendererOpenGL::Shutdown() noexcept
{
    if (!textures_.empty())
    {
        glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
        textures_.clear();
    }
    if (rendererInitialized_)
    {
        ImGui_ImplOpenGL3_Shutdown();
        rendererInitialized_ = false;
    }
    if (platformInitialized_)
    {
        ImGui_ImplSDL3_Shutdown();
        platformInitialized_ = false;
    }
}
} // namespace Horo::Editor
