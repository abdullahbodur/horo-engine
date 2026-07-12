#pragma once

namespace Horo::Editor
{
class WorkspacePanelRegistry;

/**
 * @brief Registers the default panels (Hierarchy, Inspector, Content Browser, Viewport)
 *        into the provided WorkspacePanelRegistry.
 */
void RegisterDefaultWorkspacePanels(WorkspacePanelRegistry& registry);

} // namespace Horo::Editor
