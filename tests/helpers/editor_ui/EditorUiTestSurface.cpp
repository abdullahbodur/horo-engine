#include "EditorUiTestSurface.h"

#include <imgui.h>

#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace Horo::Tests
{
namespace
{
class HeadlessEditorUiTestSurface final : public IEditorUiTestSurface
{
  public:
    void Initialize(ImGuiContext &context) override
    {
        ImGui::SetCurrentContext(&context);
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = {1280.0F, 800.0F};
        io.DisplayFramebufferScale = {1.0F, 1.0F};
        io.DeltaTime = 1.0F / 60.0F;
    }

    [[nodiscard]] bool BeginFrame() override
    {
        return true;
    }

    void Present() override
    {
    }

    void Shutdown() noexcept override
    {
    }

    [[nodiscard]] bool IsInteractive() const noexcept override
    {
        return false;
    }
};
} // namespace

std::unique_ptr<IEditorUiTestSurface> CreateHeadlessEditorUiTestSurface()
{
    return std::make_unique<HeadlessEditorUiTestSurface>();
}

#if !defined(HORO_UI_TEST_HAS_INTERACTIVE_OPENGL)
std::unique_ptr<IEditorUiTestSurface> CreateInteractiveOpenGlEditorUiTestSurface()
{
    throw std::runtime_error("Interactive UI tests require a test-enabled build with HORO_BUILD_RENDER_OPENGL=ON.");
}
#endif

std::unique_ptr<IEditorUiTestSurface> CreateEditorUiTestSurfaceFromEnvironment()
{
    const char *const requested = std::getenv("HORO_UI_TEST_PRESENTATION");
    if (requested == nullptr || requested[0] == '\0' || std::string_view{requested} == "headless")
        return CreateHeadlessEditorUiTestSurface();
    if (std::string_view{requested} == "opengl")
        return CreateInteractiveOpenGlEditorUiTestSurface();
    throw std::invalid_argument("HORO_UI_TEST_PRESENTATION must be either 'headless' or 'opengl'.");
}
} // namespace Horo::Tests
