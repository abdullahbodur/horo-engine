import { injectable } from '@theia/core/shared/inversify';
import { AbstractViewContribution } from '@theia/core/lib/browser';
import { InspectorWidget } from './InspectorWidget';

@injectable()
export class InspectorContribution extends AbstractViewContribution<InspectorWidget> {
  constructor() {
    super({
      widgetId: InspectorWidget.ID,
      widgetName: InspectorWidget.LABEL,
      defaultWidgetOptions: { area: 'right' },
      toggleCommandId: 'horo.inspector.toggle',
    });
  }
}
