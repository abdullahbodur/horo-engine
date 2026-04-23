import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { TreeWidget, TreeModel, TreeProps, TreeNode, CompositeTreeNode, SelectableTreeNode, ContextMenuRenderer } from '@theia/core/lib/browser';
import { McpClient, McpObject } from '../mcp/McpClient';
import { HoroSelectionService } from '../common/HoroSelectionService';

export interface SceneTreeNode extends CompositeTreeNode, SelectableTreeNode {
  mcpObject: McpObject;
}

export namespace SceneTreeNode {
  export function is(node: TreeNode | undefined): node is SceneTreeNode {
    return node !== undefined && 'mcpObject' in node;
  }
}

@injectable()
export class SceneHierarchyWidget extends TreeWidget {
  static readonly ID = 'horo-scene-hierarchy';
  static readonly LABEL = 'Scene';

  private searchQuery = '';
  private activeContextMenu: HTMLElement | undefined;
  private statusEl: HTMLElement | undefined;

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
    this.insertToolbar();
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

    // Dismiss context menu on any click outside it
    document.addEventListener('mousedown', () => this.dismissContextMenu());
  }

  // --- Toolbar --------------------------------------------------------------

  private insertToolbar(): void {
    const toolbar = document.createElement('div');
    toolbar.style.cssText =
      'display:flex;gap:4px;padding:4px;' +
      'background:var(--theia-sideBar-background);' +
      'border-bottom:1px solid var(--theia-contrastBorder,#333);';

    const searchInput = document.createElement('input');
    searchInput.type = 'text';
    searchInput.placeholder = 'Filter objects…';
    searchInput.style.cssText =
      'flex:1;background:var(--theia-input-background);' +
      'color:var(--theia-input-foreground);' +
      'border:1px solid var(--theia-input-border,#555);' +
      'border-radius:2px;padding:2px 6px;font-size:12px;';
    searchInput.addEventListener('input', () => {
      this.searchQuery = searchInput.value;
      this.refresh();
    });

    const addBtn = document.createElement('button');
    addBtn.title = 'Add empty object';
    addBtn.textContent = '+';
    addBtn.style.cssText =
      'padding:2px 8px;cursor:pointer;' +
      'background:var(--theia-button-background);' +
      'color:var(--theia-button-foreground);' +
      'border:none;border-radius:2px;font-size:14px;';
    addBtn.addEventListener('click', (e) => { e.stopPropagation(); this.addObject(); });

    toolbar.appendChild(searchInput);
    toolbar.appendChild(addBtn);

    // "Not connected" status bar, hidden by default
    const status = document.createElement('div');
    status.style.cssText =
      'display:none;padding:3px 8px;font-size:11px;' +
      'color:var(--theia-statusBar-prominentForeground,#f88);' +
      'background:var(--theia-statusBar-prominentBackground,#2a1a1a);';
    status.textContent = '⚠ Engine not connected';
    this.statusEl = status;

    // Prepend so the tree content scrolls below
    this.node.prepend(status);
    this.node.prepend(toolbar);
  }

  // --- Refresh / filter -----------------------------------------------------

  async refresh(): Promise<void> {
    try {
      const allObjects = await this.mcp.listObjects();
      if (this.statusEl) this.statusEl.style.display = 'none';

      const query = this.searchQuery.toLowerCase().trim();
      // When filtering, flatten hierarchy so any matching node is reachable
      const objects = query ? allObjects.filter((o) => o.name.toLowerCase().includes(query)) : allObjects;

      const root: CompositeTreeNode = {
        id: 'root',
        name: 'Scene',
        parent: undefined,
        children: this.buildTree(objects, query ? undefined : undefined, query !== ''),
        visible: false,
      };
      this.model.root = root;
    } catch {
      // Engine not running — show indicator
      if (this.statusEl) this.statusEl.style.display = 'block';
    }
  }

  private buildTree(
    objects: McpObject[],
    parentId: string | undefined,
    flat = false
  ): SceneTreeNode[] {
    // In flat (filtered) mode every matched node appears at root level
    const candidates = flat
      ? objects
      : objects.filter((o) => o.parentId === parentId);

    return candidates.map((o) => {
      const node: SceneTreeNode = {
        id: o.id,
        name: o.name,
        parent: undefined,
        mcpObject: o,
        children: flat ? [] : this.buildTree(objects, o.id),
        selected: false,
        visible: true,
      };
      return node;
    });
  }

  // --- Object actions -------------------------------------------------------

  private async addObject(): Promise<void> {
    await this.mcp.createObject('empty');
    await this.refresh();
  }

  private async renameObject(node: SceneTreeNode): Promise<void> {
    const newName = window.prompt('Rename object:', node.name);
    if (newName && newName.trim()) {
      await this.mcp.renameObject(node.mcpObject.id, newName.trim());
      await this.refresh();
    }
  }

  private async deleteObject(node: SceneTreeNode): Promise<void> {
    if (window.confirm(`Delete "${node.name}"?`)) {
      await this.mcp.deleteObject(node.mcpObject.id);
      await this.refresh();
    }
  }

  private async duplicateObject(node: SceneTreeNode): Promise<void> {
    await this.mcp.createObject(node.mcpObject.type, node.mcpObject.parentId);
    await this.refresh();
  }

  // --- Context menu (floating HTML) -----------------------------------------

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  protected override handleContextMenuEvent(node: TreeNode | undefined, event: any): void {
    event.preventDefault?.();
    event.stopPropagation?.();
    if (SceneTreeNode.is(node)) {
      const x = (event as MouseEvent).clientX ?? 0;
      const y = (event as MouseEvent).clientY ?? 0;
      this.showContextMenu(x, y, node);
    }
  }

  private showContextMenu(x: number, y: number, node: SceneTreeNode): void {
    this.dismissContextMenu();

    const menu = document.createElement('div');
    menu.style.cssText =
      `position:fixed;left:${x}px;top:${y}px;z-index:9999;` +
      'background:var(--theia-menu-background,#252526);' +
      'border:1px solid var(--theia-menu-border,#454545);' +
      'border-radius:3px;box-shadow:0 2px 8px rgba(0,0,0,.4);' +
      'min-width:150px;padding:4px 0;font-size:13px;';
    this.activeContextMenu = menu;

    const menuItems: Array<{ label: string; action: () => void }> = [
      { label: 'Add Child Object', action: () => this.addObject() },
      { label: 'Rename', action: () => this.renameObject(node) },
      { label: 'Duplicate', action: () => this.duplicateObject(node) },
      { label: 'Delete', action: () => this.deleteObject(node) },
    ];

    for (const item of menuItems) {
      const el = document.createElement('div');
      el.textContent = item.label;
      el.style.cssText =
        'padding:5px 16px;cursor:pointer;' +
        'color:var(--theia-menu-foreground,#ccc);';
      el.addEventListener('mouseenter', () => {
        el.style.background = 'var(--theia-menu-selectionBackground,#094771)';
        el.style.color = 'var(--theia-menu-selectionForeground,#fff)';
      });
      el.addEventListener('mouseleave', () => {
        el.style.background = '';
        el.style.color = 'var(--theia-menu-foreground,#ccc)';
      });
      el.addEventListener('mousedown', (e) => {
        e.stopPropagation();
        item.action();
        this.dismissContextMenu();
      });
      menu.appendChild(el);
    }

    document.body.appendChild(menu);
  }

  private dismissContextMenu(): void {
    if (this.activeContextMenu) {
      this.activeContextMenu.remove();
      this.activeContextMenu = undefined;
    }
  }

  protected override toContextMenuArgs(node: TreeNode | undefined): [SceneTreeNode] | undefined {
    if (node && SceneTreeNode.is(node)) return [node];
    return undefined;
  }
}
