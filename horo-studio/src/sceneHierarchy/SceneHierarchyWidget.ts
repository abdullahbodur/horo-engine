import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { TreeWidget, TreeModel, TreeProps, TreeNode, CompositeTreeNode, SelectableTreeNode, ContextMenuRenderer } from '@theia/core/lib/browser';
import { McpClient, McpObject } from '../mcp/McpClient';
import { HoroSelectionService } from '../common/HoroSelectionService';

export interface SceneTreeNode extends CompositeTreeNode, SelectableTreeNode {
  mcpObject: McpObject;
}

export namespace SceneTreeNode {
  export function is(node: TreeNode): node is SceneTreeNode {
    return 'mcpObject' in node;
  }
}

@injectable()
export class SceneHierarchyWidget extends TreeWidget {
  static readonly ID = 'horo-scene-hierarchy';
  static readonly LABEL = 'Scene';

  constructor(
    @inject(McpClient) private readonly mcp: McpClient,
    @inject(HoroSelectionService) private readonly selection: HoroSelectionService,
    @inject(TreeProps) props: TreeProps,
    @inject(TreeModel) model: TreeModel,
    @inject(ContextMenuRenderer) contextMenuRenderer: ContextMenuRenderer
  ) {
    super(props, model, contextMenuRenderer);
    this.id = SceneHierarchyWidget.ID;
    this.title.label = SceneHierarchyWidget.LABEL;
    this.title.caption = 'Scene Hierarchy';
    this.title.iconClass = 'codicon codicon-list-tree';
    this.title.closable = false;
  }

  @postConstruct()
  protected override init(): void {
    super.init();
    this.addClass('horo-scene-hierarchy');
    this.refresh();
    setInterval(() => this.refresh(), 2000);

    // Forward tree selection to HoroSelectionService
    this.model.onSelectionChanged((nodes) => {
      const first = nodes[0];
      if (SceneTreeNode.is(first)) {
        this.selection.select(first.mcpObject.id);
      } else {
        this.selection.select(undefined);
      }
    });
  }

  async refresh(): Promise<void> {
    try {
      const objects = await this.mcp.listObjects();
      const root: CompositeTreeNode = {
        id: 'root',
        name: 'Scene',
        parent: undefined,
        children: this.buildTree(objects, undefined),
        visible: false,
      };
      this.model.root = root;
    } catch {
      // Engine not running yet — silently skip
    }
  }

  private buildTree(
    objects: McpObject[],
    parentId: string | undefined
  ): SceneTreeNode[] {
    return objects
      .filter((o) => o.parentId === parentId)
      .map((o) => {
        const node: SceneTreeNode = {
          id: o.id,
          name: o.name,
          parent: undefined,
          mcpObject: o,
          children: this.buildTree(objects, o.id),
          selected: false,
          visible: true,
        };
        return node;
      });
  }

  protected override toContextMenuArgs(node: TreeNode): [SceneTreeNode] | undefined {
    if (SceneTreeNode.is(node)) return [node];
    return undefined;
  }
}
