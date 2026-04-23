import { injectable, inject } from '@theia/core/shared/inversify';
import { AbstractViewContribution } from '@theia/core/lib/browser';
import { Command, CommandRegistry } from '@theia/core';
import { SceneHierarchyWidget } from './SceneHierarchyWidget';

export const SCENE_REFRESH_COMMAND: Command = {
  id: 'horo.scene.refresh',
  label: 'Refresh Scene',
  category: 'Horo',
};

@injectable()
export class SceneHierarchyContribution extends AbstractViewContribution<SceneHierarchyWidget> {
  constructor() {
    super({
      widgetId: SceneHierarchyWidget.ID,
      widgetName: SceneHierarchyWidget.LABEL,
      defaultWidgetOptions: { area: 'left' },
      toggleCommandId: 'horo.scene.toggle',
    });
  }

  override async registerCommands(registry: CommandRegistry): Promise<void> {
    super.registerCommands(registry);
    registry.registerCommand(SCENE_REFRESH_COMMAND, {
      execute: async () => {
        const widget = await this.widget;
        widget.refresh();
      },
    });
  }
}
