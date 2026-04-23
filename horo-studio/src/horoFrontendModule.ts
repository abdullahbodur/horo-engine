import { ContainerModule } from '@theia/core/shared/inversify';
import {
  bindViewContribution,
  FrontendApplicationContribution,
  WidgetFactory,
} from '@theia/core/lib/browser';

import { McpClient } from './mcp/McpClient';
import { HoroSelectionService } from './common/HoroSelectionService';

import { GameViewWidget } from './gameView/GameViewWidget';
import { GameViewContribution } from './gameView/GameViewContribution';

import { SceneHierarchyWidget } from './sceneHierarchy/SceneHierarchyWidget';
import { SceneHierarchyContribution } from './sceneHierarchy/SceneHierarchyContribution';

import { InspectorWidget } from './inspector/InspectorWidget';
import { InspectorContribution } from './inspector/InspectorContribution';

import { AssetBrowserWidget } from './assetBrowser/AssetBrowserWidget';
import { AssetBrowserContribution } from './assetBrowser/AssetBrowserContribution';

export default new ContainerModule((bind) => {
  // Shared singletons
  bind(McpClient).toSelf().inSingletonScope();
  bind(HoroSelectionService).toSelf().inSingletonScope();

  // ---- Game View -----------------------------------------------------------
  bind(GameViewWidget).toSelf();
  bind(WidgetFactory)
    .toDynamicValue((ctx) => ({
      id: GameViewWidget.ID,
      createWidget: () => ctx.container.get(GameViewWidget),
    }))
    .inSingletonScope();
  bindViewContribution(bind, GameViewContribution);
  bind(FrontendApplicationContribution).toService(GameViewContribution);

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
});
