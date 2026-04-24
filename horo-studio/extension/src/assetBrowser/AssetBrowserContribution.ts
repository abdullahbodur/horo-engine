import { injectable } from '@theia/core/shared/inversify';
import { AbstractViewContribution } from '@theia/core/lib/browser';
import { AssetBrowserWidget } from './AssetBrowserWidget';

@injectable()
export class AssetBrowserContribution extends AbstractViewContribution<AssetBrowserWidget> {
  constructor() {
    super({
      widgetId: AssetBrowserWidget.ID,
      widgetName: AssetBrowserWidget.LABEL,
      defaultWidgetOptions: { area: 'bottom' },
      toggleCommandId: 'horo.assets.toggle',
    });
  }
}
