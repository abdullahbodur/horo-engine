import { ContainerModule } from '@theia/core/shared/inversify';
import {
  bindViewContribution,
  FrontendApplicationContribution,
  WidgetFactory,
  KeybindingContribution,
} from '@theia/core/lib/browser';
import { CommandContribution, MenuContribution } from '@theia/core';

import { McpClient } from './mcp/McpClient';
import { HoroSelectionService } from './common/HoroSelectionService';
import { HoroCommandsContribution } from './toolbar/HoroCommandsContribution';

import { GameViewWidget } from './gameView/GameViewWidget';
import { GameViewContribution } from './gameView/GameViewContribution';

import { SceneHierarchyWidget } from './sceneHierarchy/SceneHierarchyWidget';
import { SceneHierarchyContribution } from './sceneHierarchy/SceneHierarchyContribution';

import { InspectorWidget } from './inspector/InspectorWidget';
import { InspectorContribution } from './inspector/InspectorContribution';

import { AssetBrowserWidget } from './assetBrowser/AssetBrowserWidget';
import { AssetBrowserContribution } from './assetBrowser/AssetBrowserContribution';

import { ConsoleWidget } from './console/ConsoleWidget';
import { ConsoleContribution } from './console/ConsoleContribution';
import { HoroLayoutContribution } from './layout/HoroLayoutContribution';

export default new ContainerModule((bind) => {
  console.info('[horo-module] registering horo frontend module');
  // Shared singletons
  bind(McpClient).toSelf().inSingletonScope();
  bind(HoroSelectionService).toSelf().inSingletonScope();

  // ---- Commands, menus, keybindings ----------------------------------------
  bind(HoroCommandsContribution).toSelf().inSingletonScope();
  bind(CommandContribution).toService(HoroCommandsContribution);
  bind(MenuContribution).toService(HoroCommandsContribution);
  bind(KeybindingContribution).toService(HoroCommandsContribution);

  // ---- Game View -----------------------------------------------------------
  bind(GameViewWidget).toSelf();
  bind(WidgetFactory)
    .toDynamicValue((ctx) => ({
      id: GameViewWidget.ID,
      createWidget: () => ctx.container.get(GameViewWidget),
    }))
    .inSingletonScope();
  bindViewContribution(bind, GameViewContribution);

  // ---- Scene Hierarchy -----------------------------------------------------
  bind(SceneHierarchyWidget).toSelf();
  bind(WidgetFactory)
    .toDynamicValue((ctx) => ({
      id: SceneHierarchyWidget.ID,
      createWidget: () => ctx.container.get(SceneHierarchyWidget),
    }))
    .inSingletonScope();
  bindViewContribution(bind, SceneHierarchyContribution);

  // ---- Inspector -----------------------------------------------------------
  bind(InspectorWidget).toSelf();
  bind(WidgetFactory)
    .toDynamicValue((ctx) => ({
      id: InspectorWidget.ID,
      createWidget: () => ctx.container.get(InspectorWidget),
    }))
    .inSingletonScope();
  bindViewContribution(bind, InspectorContribution);

  // ---- Asset Browser -------------------------------------------------------
  bind(AssetBrowserWidget).toSelf();
  bind(WidgetFactory)
    .toDynamicValue((ctx) => ({
      id: AssetBrowserWidget.ID,
      createWidget: () => ctx.container.get(AssetBrowserWidget),
    }))
    .inSingletonScope();
  bindViewContribution(bind, AssetBrowserContribution);

  // ---- Console -------------------------------------------------------------
  bind(ConsoleWidget).toSelf();
  bind(WidgetFactory)
    .toDynamicValue((ctx) => ({
      id: ConsoleWidget.ID,
      createWidget: () => ctx.container.get(ConsoleWidget),
    }))
    .inSingletonScope();
  bindViewContribution(bind, ConsoleContribution);

  // ---- Deterministic startup layout ---------------------------------------
  bind(HoroLayoutContribution).toSelf().inSingletonScope();
  bind(FrontendApplicationContribution).toService(HoroLayoutContribution);
});
