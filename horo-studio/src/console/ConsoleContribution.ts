import { injectable } from '@theia/core/shared/inversify';
import {
  AbstractViewContribution,
  FrontendApplicationContribution,
} from '@theia/core/lib/browser';
import { ConsoleWidget } from './ConsoleWidget';

@injectable()
export class ConsoleContribution
  extends AbstractViewContribution<ConsoleWidget>
  implements FrontendApplicationContribution
{
  constructor() {
    super({
      widgetId: ConsoleWidget.ID,
      widgetName: ConsoleWidget.LABEL,
      defaultWidgetOptions: { area: 'bottom' },
      toggleCommandId: 'horo.console.toggle',
    });
  }

  async onStart(): Promise<void> {
    this.openView({ activate: false });
  }
}
