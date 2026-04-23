import { injectable, inject } from '@theia/core/shared/inversify';
import {
  Command, CommandRegistry, CommandContribution,
  MenuContribution, MenuModelRegistry,
  MessageService,
} from '@theia/core';
import { KeybindingContribution, KeybindingRegistry, CommonMenus } from '@theia/core/lib/browser';
import { McpClient } from '../mcp/McpClient';

export const HORO_SAVE_SCENE: Command = { id: 'horo.scene.save', label: 'Save Scene', category: 'Horo' };
export const HORO_RELOAD_SCENE: Command = { id: 'horo.scene.reload', label: 'Reload Scene', category: 'Horo' };
export const HORO_NEW_SCENE: Command = { id: 'horo.scene.new', label: 'New Scene', category: 'Horo' };
export const HORO_REFRESH_ALL: Command = { id: 'horo.refresh', label: 'Refresh Panels', category: 'Horo' };

@injectable()
export class HoroCommandsContribution implements CommandContribution, MenuContribution, KeybindingContribution {
  constructor(
    @inject(McpClient) private readonly mcp: McpClient,
    @inject(MessageService) private readonly messages: MessageService,
  ) {}

  registerCommands(registry: CommandRegistry): void {
    registry.registerCommand(HORO_SAVE_SCENE, {
      execute: async () => {
        const result = await this.mcp.saveScene();
        if (result.ok) {
          this.messages.info('Scene saved.');
        } else {
          this.messages.error(`Save failed: ${result.error ?? 'unknown error'}`);
        }
      }
    });

    registry.registerCommand(HORO_RELOAD_SCENE, {
      execute: async () => {
        const result = await this.mcp.reloadScene();
        if (result.ok) {
          this.messages.info('Scene reloaded.');
        } else {
          this.messages.error(`Reload failed: ${result.error ?? 'unknown error'}`);
        }
      }
    });

    registry.registerCommand(HORO_NEW_SCENE, {
      execute: async () => {
        const result = await this.mcp.newScene();
        if (result.ok) {
          this.messages.info('New scene created.');
        } else {
          this.messages.error(`New scene failed: ${result.error ?? 'unknown error'}`);
        }
      }
    });

    registry.registerCommand(HORO_REFRESH_ALL, {
      execute: () => {
        // Panels poll on their own; this command is for manual refresh
        this.messages.info('Panels will refresh on next poll cycle.');
      }
    });
  }

  registerMenus(menus: MenuModelRegistry): void {
    menus.registerMenuAction(CommonMenus.FILE, {
      commandId: HORO_NEW_SCENE.id,
      order: 'a09',
    });
    menus.registerMenuAction(CommonMenus.FILE, {
      commandId: HORO_SAVE_SCENE.id,
      order: 'a10',
    });
    menus.registerMenuAction(CommonMenus.FILE, {
      commandId: HORO_RELOAD_SCENE.id,
      order: 'a11',
    });
  }

  registerKeybindings(registry: KeybindingRegistry): void {
    registry.registerKeybinding({
      command: HORO_SAVE_SCENE.id,
      keybinding: 'ctrlcmd+s',
    });
    registry.registerKeybinding({
      command: HORO_RELOAD_SCENE.id,
      keybinding: 'ctrlcmd+shift+r',
    });
  }
}
