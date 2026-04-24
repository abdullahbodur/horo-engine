import { injectable, inject } from '@theia/core/shared/inversify';
import {
  ApplicationShell,
  FrontendApplicationContribution,
  WidgetManager,
} from '@theia/core/lib/browser';
import { FrontendApplicationStateService } from '@theia/core/lib/browser/frontend-application-state';

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
    @inject(FrontendApplicationStateService)
    private readonly stateService: FrontendApplicationStateService,
  ) {}

  // onStart schedules layout enforcement to run AFTER Theia's restoreLayout().
  // If we attach widgets directly here, Theia's layout restore overwrites them.
  // reachedState('ready') fires after restoreLayout + shell revelation, so our
  // widgets are guaranteed to appear on every launch regardless of saved state.
  onStart(): void {
    console.info('[horo-layout] onStart: scheduling layout for state=ready');
    this.stateService.reachedState('ready').then(() => {
      console.info('[horo-layout] state=ready reached, enforcing horo layout');
      this.enforceLayout().catch((e) =>
        console.error('[horo-layout] enforceLayout error', e),
      );
    });
  }

  private async enforceLayout(): Promise<void> {
    await this.attachIfNeeded(SceneHierarchyWidget.ID, { area: 'left' });
    const sceneWidget = this.shell.getWidgetById(SceneHierarchyWidget.ID);
    await this.attachIfNeeded(AssetBrowserWidget.ID, {
      area: 'left',
      mode: 'split-bottom',
      ref: sceneWidget,
    });
    await this.attachIfNeeded(InspectorWidget.ID, { area: 'right' });
    await this.attachIfNeeded(GameViewWidget.ID, { area: 'main' });
    await this.attachIfNeeded(ConsoleWidget.ID, { area: 'bottom' });

    const navigator = this.shell.getWidgetById('files');
    if (navigator?.isAttached) {
      navigator.close();
      console.info('[horo-layout] closed default files navigator');
    }
    await this.shell.activateWidget(GameViewWidget.ID);
    console.info('[horo-layout] enforceLayout done');
  }

  private async attachIfNeeded(
    widgetId: string,
    options: ApplicationShell.WidgetOptions,
  ): Promise<void> {
    const widget = await this.widgetManager.getOrCreateWidget(widgetId);
    if (!widget.isAttached) {
      this.shell.addWidget(widget, options);
      console.info(`[horo-layout] attached: ${widgetId}`);
    } else {
      console.info(`[horo-layout] already attached: ${widgetId}`);
    }
  }
}
