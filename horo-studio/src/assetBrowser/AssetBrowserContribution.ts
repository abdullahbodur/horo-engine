import { injectable } from '@theia/core/shared/inversify';
import { AbstractViewContribution, FrontendApplicationContribution } from '@theia/core/lib/browser';
import { AssetBrowserWidget } from './AssetBrowserWidget';

@injectable()
export class AssetBrowserContribution extends AbstractViewContribution<AssetBrowserWidget> implements FrontendApplicationContribution {
  constructor() {
    super({
      widgetId: AssetBrowserWidget.ID,
      widgetName: AssetBrowserWidget.LABEL,
      defaultWidgetOptions: { area: 'bottom' },
      toggleCommandId: 'horo.assets.toggle',
    });
  }

  async onStart(): Promise<void> {
    await this.openView({ activate: false, reveal: true });
  }

  async onDidInitializeLayout(): Promise<void> {
    await this.openView({ activate: false, reveal: true });
  }
}
