#include "editor/EditorImGuiBackend.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace Monolith::Editor {

bool IsSupportedEditorImGuiBackend(RenderBackendId backendId) {
  switch (backendId) {
    case RenderBackendId::Auto:
    case RenderBackendId::OpenGL:
      return true;
    case RenderBackendId::Vulkan:
      return false;
  }

  return false;
}

bool InitEditorImGuiBackend(GLFWwindow* window, RenderBackendId backendId) {
  if (!window || !IsSupportedEditorImGuiBackend(backendId))
    return false;

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410");
  return true;
}

void ShutdownEditorImGuiBackend(RenderBackendId backendId) {
  if (!IsSupportedEditorImGuiBackend(backendId))
    return;

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
}

void BeginEditorImGuiFrame(RenderBackendId backendId) {
  if (!IsSupportedEditorImGuiBackend(backendId))
    return;

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
}

void RenderEditorImGuiDrawData(RenderBackendId backendId, ImDrawData* drawData) {
  if (!IsSupportedEditorImGuiBackend(backendId) || !drawData)
    return;

  ImGui_ImplOpenGL3_RenderDrawData(drawData);
}

}  // namespace Monolith::Editor
