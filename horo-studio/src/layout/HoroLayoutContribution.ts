import { injectable, inject } from '@theia/core/shared/inversify';
import {
  ApplicationShell,
  FrontendApplicationContribution,
  WidgetManager,
} from '@theia/core/lib/browser';

import { GameViewWidget } from '../gameView/GameViewWidget';
import { SceneHierarchyWidget } from '../sceneHierarchy/SceneHierarchyWidget';
import { InspectorWidget } from '../inspector/InspectorWidget';
import { AssetBrowserWidget } from '../assetBrowser/AssetBrowserWidget';
import { ConsoleWidget } from '../console/ConsoleWidget';

@injectable()
export class HoroLayoutContribution implements FrontendApplicationContribution {
  constructor(
    @inject(ApplicationShell) private readonly shell: ApplicationShell,
    @inject(WidgetManager) private readonly widgetManager: WidgetManager,
  ) {}

  // onStart runs on every launch (not just on first init). This ensures our
  // custom widgets are always present even when Theia restores a saved session
  // that pre-dates our widgets. attachIfNeeded guards against duplicates.
  async onStart(): Promise<void> {
    console.info('[horo-layout] onStart: enforcing horo layout');
    await this.attachIfNeeded(SceneHierarchyWidget.ID, { area: 'left' });
    await this.attachIfNeeded(AssetBrowserWidget.ID, {
      area: 'left',
      mode: 'split-bottom',
      ref: SceneHierarchyWidget.ID,
    });
    await this.attachIfNeeded(InspectorWidget.ID, { area: 'right' });
    await this.attachIfNeeded(GameViewWidget.ID, { area: 'main' });
    await this.attachIfNeeded(ConsoleWidget.ID, { area: 'bottom' });

    const navigator = this.shell.getWidgetById('files');
    navigator?.close();
    this.shell.activateWidget(GameViewWidget.ID);
    console.info('[horo-layout] onStart: layout enforced');
  }

  private async attachIfNeeded(
    widgetId: string,
    options: any,
  ): Promise<void> {
    const widget = await this.widgetManager.getOrCreateWidget(widgetId);
    if (!widget.isAttached) {
      this.shell.addWidget(widget, options);
      console.info(`[horo-layout] attached widget: ${widgetId}`);
    } else {
      console.info(`[horo-layout] widget already attached: ${widgetId}`);
    }
  }
}
