import { injectable } from '@theia/core/shared/inversify';
import {
  AbstractViewContribution,
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
  }

  override async registerMenus(menus: MenuModelRegistry): Promise<void> {
    super.registerMenus(menus);
  }
}
