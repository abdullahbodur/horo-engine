import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { BaseWidget } from '@theia/core/lib/browser';
import { McpClient, McpAsset } from '../mcp/McpClient';

@injectable()
export class AssetBrowserWidget extends BaseWidget {
  static readonly ID = 'horo-asset-browser';
  static readonly LABEL = 'Assets';

  constructor(@inject(McpClient) private readonly mcp: McpClient) {
    super();
    this.id = AssetBrowserWidget.ID;
    this.title.label = AssetBrowserWidget.LABEL;
    this.title.caption = 'Asset Browser';
    this.title.iconClass = 'codicon codicon-folder';
    this.title.closable = false;
    this.addClass('horo-asset-browser');
    this.node.style.cssText = 'height:100%;overflow:auto;padding:4px 0;';
  }

  @postConstruct()
  protected init(): void {
    this.refresh();
  }

  async refresh(): Promise<void> {
    try {
      const assets = await this.mcp.listAssets();
      const byType: Record<string, McpAsset[]> = {};
      for (const a of assets) {
        (byType[a.type] ??= []).push(a);
      }
      this.node.innerHTML = '';

      if (Object.keys(byType).length === 0) {
        const empty = document.createElement('div');
        empty.textContent = 'No assets — engine not connected or scene is empty.';
        empty.style.cssText = 'padding:8px;font-size:12px;opacity:0.5;color:var(--theia-foreground);';
        this.node.appendChild(empty);
        return;
      }

      for (const [type, items] of Object.entries(byType)) {
        const header = document.createElement('div');
        header.textContent = type.charAt(0).toUpperCase() + type.slice(1) + 's';
        header.style.cssText =
          'padding:4px 8px 2px;font-size:11px;text-transform:uppercase;' +
          'color:var(--theia-foreground);opacity:0.55;letter-spacing:0.05em;';
        this.node.appendChild(header);

        for (const asset of items) {
          const el = document.createElement('div');
          el.textContent = asset.name || asset.displayName || asset.id;
          el.title = asset.id;
          el.style.cssText =
            'padding:3px 8px 3px 20px;cursor:pointer;font-size:12px;' +
            'color:var(--theia-foreground);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;';
          el.addEventListener('mouseenter', () => { el.style.background = 'var(--theia-list-hoverBackground,#2a2d2e)'; });
          el.addEventListener('mouseleave', () => { el.style.background = ''; });
          el.addEventListener('click', () => this.mcp.selectAsset(asset.id));
          this.node.appendChild(el);
        }
      }
    } catch {
      this.node.innerHTML =
        '<div style="padding:8px;color:#f88;font-size:12px;">⚠ Engine not connected</div>';
    }
  }
}
