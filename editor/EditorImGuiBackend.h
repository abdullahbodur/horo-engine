#pragma once

#include "renderer/RenderBackend.h"

struct GLFWwindow;
struct ImDrawData;

namespace Horo::Editor {
    bool IsSupportedEditorImGuiBackend(RenderBackendId backendId);

    bool InitEditorImGuiBackend(GLFWwindow *window, RenderBackendId backendId);

    void ShutdownEditorImGuiBackend(RenderBackendId backendId);

    void BeginEditorImGuiFrame(RenderBackendId backendId);

    void RenderEditorImGuiDrawData(RenderBackendId backendId, ImDrawData *drawData);
} // namespace Horo::Editor
