/** @file EditorImGuiBackend.h
 *  @brief ImGui render-backend lifecycle management for the in-game editor. */
#pragma once

#include "renderer/RenderBackend.h"

struct GLFWwindow;
struct ImDrawData;

namespace Horo::Editor {
    /** @brief Returns true when @p backendId is a render backend the editor ImGui integration supports. */
    bool IsSupportedEditorImGuiBackend(RenderBackendId backendId);

    /** @brief Initialises the ImGui platform and renderer backends for the editor.
     *  @param window    The GLFW window that owns the render context.
     *  @param backendId The active render backend identifier.
     *  @return True on success; false when the backend is unsupported or initialisation failed. */
    bool InitEditorImGuiBackend(GLFWwindow *window, RenderBackendId backendId);

    /** @brief Tears down the ImGui render backend and releases its resources.
     *  @param backendId The active render backend identifier. */
    void ShutdownEditorImGuiBackend(RenderBackendId backendId);

    /** @brief Signals the start of a new ImGui frame to the render backend.
     *  @param backendId The active render backend identifier. */
    void BeginEditorImGuiFrame(RenderBackendId backendId);

    /** @brief Submits ImGui draw data produced by ImGui::Render() to the GPU.
     *  @param backendId The active render backend identifier.
     *  @param drawData  The draw data returned by ImGui::GetDrawData(). */
    void RenderEditorImGuiDrawData(RenderBackendId backendId, ImDrawData *drawData);
} // namespace Horo::Editor
