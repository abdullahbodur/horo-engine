import { injectable } from '@theia/core/shared/inversify';
import { AbstractViewContribution, FrontendApplicationContribution } from '@theia/core/lib/browser';
import { InspectorWidget } from './InspectorWidget';

@injectable()
export class InspectorContribution extends AbstractViewContribution<InspectorWidget> implements FrontendApplicationContribution {
  constructor() {
    super({
      widgetId: InspectorWidget.ID,
      widgetName: InspectorWidget.LABEL,
      defaultWidgetOptions: { area: 'right' },
      toggleCommandId: 'horo.inspector.toggle',
    });
  }

  async onStart(): Promise<void> {
    this.openView({ activate: false });
  }
}
