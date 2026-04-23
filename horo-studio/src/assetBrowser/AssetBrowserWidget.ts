import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { TreeWidget, TreeModel, TreeProps, TreeNode, CompositeTreeNode, SelectableTreeNode, ContextMenuRenderer } from '@theia/core/lib/browser';
import { McpClient, McpAsset } from '../mcp/McpClient';

export interface AssetTreeNode extends SelectableTreeNode {
  asset: McpAsset;
}

export namespace AssetTreeNode {
  export function is(node: TreeNode): node is AssetTreeNode {
    return 'asset' in node;
  }
}

/** Groups assets by their 'type' field into expandable categories. */
@injectable()
export class AssetBrowserWidget extends TreeWidget {
  static readonly ID = 'horo-asset-browser';
  static readonly LABEL = 'Assets';

  constructor(
    @inject(McpClient) private readonly mcp: McpClient,
    @inject(TreeProps) props: TreeProps,
    @inject(TreeModel) model: TreeModel,
    @inject(ContextMenuRenderer) contextMenuRenderer: ContextMenuRenderer
  ) {
    super(props, model, contextMenuRenderer);
    this.id = AssetBrowserWidget.ID;
    this.title.label = AssetBrowserWidget.LABEL;
    this.title.caption = 'Asset Browser';
    this.title.iconClass = 'codicon codicon-folder';
    this.title.closable = false;
  }

  @postConstruct()
  protected override init(): void {
    super.init();
    this.addClass('horo-asset-browser');
    this.refresh();
  }

  async refresh(): Promise<void> {
    try {
      const assets = await this.mcp.listAssets();
      const byType = this.groupByType(assets);

      const root: CompositeTreeNode = {
        id: 'assets-root',
        name: 'Assets',
        parent: undefined,
        children: Object.entries(byType).map(([type, items]) => {
          const group: CompositeTreeNode = {
            id: `group-${type}`,
            name: type.charAt(0).toUpperCase() + type.slice(1),
            parent: undefined,
            visible: true,
            children: items.map((a): AssetTreeNode => ({
              id: a.id,
              name: a.name,
              parent: undefined,
              asset: a,
              visible: true,
              selected: false,
            })),
          };
          return group;
        }),
        visible: false,
      };
      this.model.root = root;
    } catch {
      // Engine not running
    }
  }

  private groupByType(assets: McpAsset[]): Record<string, McpAsset[]> {
    const map: Record<string, McpAsset[]> = {};
    for (const a of assets) {
      (map[a.type] ??= []).push(a);
    }
    return map;
  }
}
