#pragma once

#include "renderer/RenderBackend.h"

struct GLFWwindow;
struct ImDrawData;

namespace Monolith::Editor {

bool IsSupportedEditorImGuiBackend(RenderBackendId backendId);
RenderBackendId ResolveEditorImGuiBackend(RenderBackendId backendId, int glfwClientApi);
bool InitEditorImGuiBackend(GLFWwindow* window, RenderBackendId backendId);
void ShutdownEditorImGuiBackend(RenderBackendId backendId);
void BeginEditorImGuiFrame(RenderBackendId backendId);
void RenderEditorImGuiDrawData(RenderBackendId backendId, ImDrawData* drawData);

}  // namespace Monolith::Editor
