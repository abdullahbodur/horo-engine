import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { BaseWidget } from '@theia/core/lib/browser';
import { McpClient, McpObject } from '../mcp/McpClient';
import { HoroSelectionService } from '../common/HoroSelectionService';

@injectable()
export class SceneHierarchyWidget extends BaseWidget {
  static readonly ID = 'horo-scene-hierarchy';
  static readonly LABEL = 'Scene';

  private searchQuery = '';
  private treeEl: HTMLElement | null = null;
  private activeMenu: HTMLElement | null = null;
  private selectedId: string | undefined;

  constructor(
    @inject(McpClient) private readonly mcp: McpClient,
    @inject(HoroSelectionService) private readonly selection: HoroSelectionService,
  ) {
    super();
    this.id = SceneHierarchyWidget.ID;
    this.title.label = SceneHierarchyWidget.LABEL;
    this.title.caption = 'Scene Hierarchy';
    this.title.iconClass = 'codicon codicon-list-tree';
    this.title.closable = false;
    this.addClass('horo-scene-hierarchy');
    this.node.style.cssText = 'display:flex;flex-direction:column;height:100%;overflow:hidden;';
  }

  @postConstruct()
  protected init(): void {
    // Toolbar
    const toolbar = document.createElement('div');
    toolbar.style.cssText =
      'display:flex;gap:4px;padding:4px;flex-shrink:0;' +
      'background:var(--theia-sideBar-background);' +
      'border-bottom:1px solid var(--theia-contrastBorder,#333);';

    const searchInput = document.createElement('input');
    searchInput.type = 'text';
    searchInput.placeholder = 'Filter objects…';
    searchInput.style.cssText =
      'flex:1;background:var(--theia-input-background);color:var(--theia-input-foreground);' +
      'border:1px solid var(--theia-input-border,#555);border-radius:2px;padding:2px 6px;font-size:12px;';
    searchInput.addEventListener('input', () => {
      this.searchQuery = searchInput.value;
      this.refresh();
    });

    const addBtn = document.createElement('button');
    addBtn.textContent = '+';
    addBtn.title = 'Add empty object';
    addBtn.style.cssText =
      'padding:2px 8px;cursor:pointer;font-size:14px;' +
      'background:var(--theia-button-background);color:var(--theia-button-foreground);' +
      'border:none;border-radius:2px;';
    addBtn.addEventListener('click', () => {
      this.mcp.createObject('empty').then(() => this.refresh());
    });

    toolbar.appendChild(searchInput);
    toolbar.appendChild(addBtn);

    this.treeEl = document.createElement('div');
    this.treeEl.style.cssText = 'flex:1;overflow:auto;';

    this.node.appendChild(toolbar);
    this.node.appendChild(this.treeEl);

    this.refresh();
    setInterval(() => this.refresh(), 2000);
  }

  async refresh(): Promise<void> {
    if (!this.treeEl) return;
    try {
      const all = await this.mcp.listObjects();
      const query = this.searchQuery.toLowerCase().trim();
      const filtered = query ? all.filter(o => o.name.toLowerCase().includes(query)) : all;
      const roots = query ? filtered : filtered.filter(o => !o.parentId);

      this.treeEl.innerHTML = '';
      this.renderItems(filtered, roots, 0);
    } catch {
      if (this.treeEl) {
        this.treeEl.innerHTML =
          '<div style="padding:8px;color:#f88;font-size:12px;">⚠ Engine not connected</div>';
      }
    }
  }

  private renderItems(all: McpObject[], items: McpObject[], depth: number): void {
    for (const obj of items) {
      const el = document.createElement('div');
      const indent = 8 + depth * 16;
      el.style.cssText =
        `padding:3px 8px 3px ${indent}px;cursor:pointer;font-size:13px;` +
        'color:var(--theia-foreground);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;';
      el.textContent = obj.name;
      el.title = obj.name;

      if (this.selectedId === obj.id) {
        el.style.background = 'var(--theia-list-activeSelectionBackground,#094771)';
      }

      el.addEventListener('click', () => {
        this.selectedId = obj.id;
        this.selection.select(obj.id);
        this.refresh();
      });
      el.addEventListener('mouseenter', () => {
        if (this.selectedId !== obj.id) el.style.background = 'var(--theia-list-hoverBackground,#2a2d2e)';
      });
      el.addEventListener('mouseleave', () => {
        if (this.selectedId !== obj.id) el.style.background = '';
      });
      el.addEventListener('contextmenu', (e) => {
        e.preventDefault();
        this.showContextMenu(e.clientX, e.clientY, obj);
      });

      this.treeEl!.appendChild(el);

      const children = all.filter(o => o.parentId === obj.id);
      if (children.length > 0) this.renderItems(all, children, depth + 1);
    }
  }

  private showContextMenu(x: number, y: number, obj: McpObject): void {
    this.activeMenu?.remove();
    const menu = document.createElement('div');
    menu.style.cssText =
      `position:fixed;left:${x}px;top:${y}px;z-index:9999;` +
      'background:var(--theia-menu-background,#252526);' +
      'border:1px solid var(--theia-menu-border,#454545);' +
      'border-radius:3px;min-width:140px;padding:4px 0;font-size:13px;box-shadow:0 2px 8px rgba(0,0,0,.4);';
    this.activeMenu = menu;

    const menuItems: Array<[string, () => void]> = [
      ['Add Child', () => this.mcp.createObject('empty', obj.id).then(() => this.refresh())],
      ['Rename', () => {
        const n = window.prompt('Rename:', obj.name);
        if (n?.trim()) this.mcp.renameObject(obj.id, n.trim()).then(() => this.refresh());
      }],
      ['Duplicate', () => this.mcp.createObject(obj.type, obj.parentId).then(() => this.refresh())],
      ['Delete', () => {
        if (window.confirm(`Delete "${obj.name}"?`)) this.mcp.deleteObject(obj.id).then(() => this.refresh());
      }],
    ];

    for (const [label, action] of menuItems) {
      const item = document.createElement('div');
      item.textContent = label;
      item.style.cssText = 'padding:5px 16px;cursor:pointer;color:var(--theia-menu-foreground,#ccc);';
      item.addEventListener('mouseenter', () => { item.style.background = 'var(--theia-menu-selectionBackground,#094771)'; });
      item.addEventListener('mouseleave', () => { item.style.background = ''; });
      item.addEventListener('mousedown', (e) => { e.stopPropagation(); action(); menu.remove(); });
      menu.appendChild(item);
    }

    document.body.appendChild(menu);
    document.addEventListener('mousedown', () => { menu.remove(); this.activeMenu = null; }, { once: true });
  }
}
