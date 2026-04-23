import { injectable, inject } from '@theia/core/shared/inversify';
import {
  AbstractViewContribution,
  FrontendApplicationContribution,
  Widget,
} from '@theia/core/lib/browser';
import { Command, CommandRegistry, MenuModelRegistry } from '@theia/core';
import { GameViewWidget } from './GameViewWidget';

export const GAME_VIEW_TOGGLE_COMMAND: Command = {
  id: 'horo.gameView.open',
  label: 'Open Game View',
  category: 'Horo',
};

@injectable()
export class GameViewContribution
  extends AbstractViewContribution<GameViewWidget>
  implements FrontendApplicationContribution
{
  constructor() {
    super({
      widgetId: GameViewWidget.ID,
      widgetName: 'Game View',
      defaultWidgetOptions: { area: 'main' },
      toggleCommandId: GAME_VIEW_TOGGLE_COMMAND.id,
    });
  }

  override async registerCommands(registry: CommandRegistry): Promise<void> {
    super.registerCommands(registry);
    registry.registerCommand(GAME_VIEW_TOGGLE_COMMAND, {
      execute: () => this.openView({ activate: true }),
    });
  }

  override async registerMenus(menus: MenuModelRegistry): Promise<void> {
    super.registerMenus(menus);
  }

  async onStart(): Promise<void> {
    // Auto-open game view on startup
    this.openView({ activate: false });
  }
}
