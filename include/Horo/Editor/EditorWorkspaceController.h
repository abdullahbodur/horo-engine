#pragma once

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorWorkspaceViewModel.h"

#include <string>

namespace Horo::Editor
{
class EditorWorkspaceController
{
  public:
    EditorWorkspaceController(std::string projectRoot);
    ~EditorWorkspaceController() = default;

    [[nodiscard]] const EditorWorkspaceViewModel &ViewModel() const noexcept
    {
        return m_viewModel;
    }
    [[nodiscard]] EditorDataBus &DataBus() noexcept
    {
        return m_dataBus;
    }

    void ProcessCommand(const EditorWorkspaceViewCommandData &cmd);
    void UpdateFps(float fps);

  private:
    EditorWorkspaceViewModel m_viewModel;
    EditorDataBus m_dataBus;

    void HandleAddObject();
    void HandleDuplicateObject(int index);
    void HandleDeleteObject(int index);
};
} // namespace Horo::Editor
