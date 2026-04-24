import { injectable } from '@theia/core/shared/inversify';
import { AbstractViewContribution } from '@theia/core/lib/browser';
import { ProjectBrowserWidget } from './ProjectBrowserWidget';

@injectable()
export class ProjectBrowserContribution extends AbstractViewContribution<ProjectBrowserWidget> {
  constructor() {
    super({
      widgetId: ProjectBrowserWidget.ID,
      widgetName: ProjectBrowserWidget.LABEL,
      defaultWidgetOptions: { area: 'left' },
      toggleCommandId: 'horo.project.toggle',
    });
  }
}
