import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { BaseWidget, Message } from '@theia/core/lib/browser';
import { McpClient, McpObject } from '../mcp/McpClient';
import { HoroSelectionService } from '../common/HoroSelectionService';

/**
 * Inspector widget — shows and edits properties of the selected entity.
 * Uses a simple HTML form rendered inside the widget node.
 * Reacts to HoroSelectionService changes fired by SceneHierarchyWidget.
 */
@injectable()
export class InspectorWidget extends BaseWidget {
  static readonly ID = 'horo-inspector';
  static readonly LABEL = 'Inspector';

  private currentObject: McpObject | null = null;

  constructor(
    @inject(McpClient) private readonly mcp: McpClient,
    @inject(HoroSelectionService) private readonly selection: HoroSelectionService,
  ) {
    super();
  }

  @postConstruct()
  protected init(): void {
    this.id = InspectorWidget.ID;
    this.title.label = InspectorWidget.LABEL;
    this.title.caption = 'Entity Inspector';
    this.title.iconClass = 'codicon codicon-inspect';
    this.title.closable = false;
    this.addClass('horo-inspector-widget');
    this.node.style.overflow = 'auto';
    this.node.style.padding = '8px';
    this.node.style.fontFamily = 'var(--theia-ui-font-family)';
    this.node.style.fontSize = '12px';
    this.node.style.color = 'var(--theia-foreground)';
    this.renderEmpty();

    // React to scene selection changes
    this.toDispose.push(
      this.selection.onSelectionChanged((id) => {
        if (id) {
          this.showObject(id);
        } else {
          this.clearSelection();
        }
      })
    );
  }

  protected onAfterAttach(_msg: Message): void {
    // If something is already selected when we attach, show it immediately
    if (this.selection.selectedId) {
      this.showObject(this.selection.selectedId);
    }
  }

  private async showObject(id: string): Promise<void> {
    try {
      const obj = await this.mcp.getObject(id);
      this.currentObject = obj;
      this.renderObject(obj);
    } catch {
      this.renderEmpty();
    }
  }

  private clearSelection(): void {
    this.currentObject = null;
    this.renderEmpty();
  }

  // ---- HTML rendering ------------------------------------------------------

  private renderEmpty(): void {
    this.node.innerHTML = `
      <div style="color: var(--theia-descriptionForeground); margin-top: 16px; text-align: center;">
        Select an entity in the Scene hierarchy.
      </div>`;
  }

  private renderObject(obj: McpObject | null): void {
    if (!obj) { this.renderEmpty(); return; }

    const p = obj.position ?? { x: 0, y: 0, z: 0 };
    const r = obj.rotation ?? { x: 0, y: 0, z: 0 };
    const s = obj.scale ?? { x: 1, y: 1, z: 1 };

    const sectionHeader = (title: string) =>
      `<div style="color: var(--theia-settings-headerForeground); font-weight: 600; font-size: 11px; text-transform: uppercase; letter-spacing: 0.05em; margin: 12px 0 4px;">${title}</div>`;

    const vec3Row = (prefix: string, x: number, y: number, z: number) => {
      const inp = (axis: string, val: number) =>
        `<div style="flex: 1; min-width: 0;">
          <div style="color: var(--theia-descriptionForeground); font-size: 10px; margin-bottom: 1px;">${axis}</div>
          <input data-key="${prefix}.${axis.toLowerCase()}"
            style="background: var(--theia-input-background); color: var(--theia-input-foreground); border: 1px solid var(--theia-input-border); padding: 2px 4px; width: 100%; box-sizing: border-box; font-size: 12px;"
            type="number" step="any" value="${val}" />
        </div>`;
      return `<div style="display: flex; gap: 4px;">${inp('X', x)}${inp('Y', y)}${inp('Z', z)}</div>`;
    };

    const componentsHtml = (): string => {
      if (!obj.components || Object.keys(obj.components).length === 0) return '';
      let html = sectionHeader('Components');
      for (const [compName, compData] of Object.entries(obj.components)) {
        html += `<div style="margin-bottom: 8px;">
          <div style="color: var(--theia-foreground); font-weight: 600; font-size: 11px; margin: 4px 0 2px; padding: 2px 4px; background: var(--theia-sideBar-background);">${this.escape(compName)}</div>`;
        if (compData && typeof compData === 'object') {
          html += `<table style="width:100%; border-collapse: collapse;">`;
          for (const [propKey, propVal] of Object.entries(compData as Record<string, unknown>)) {
            html += `<tr>
              <td style="padding: 2px 6px 2px 0; color: var(--theia-descriptionForeground); width: 80px; vertical-align: middle;">${this.escape(propKey)}</td>
              <td><input
                data-key="component.${this.escape(compName)}.${this.escape(propKey)}"
                style="width:100%; background: var(--theia-input-background); color: var(--theia-input-foreground); border: 1px solid var(--theia-input-border); padding: 2px 4px; font-size: 12px; box-sizing: border-box;"
                value="${this.escape(String(propVal))}"
                readonly
              /></td>
            </tr>`;
          }
          html += `</table>`;
        }
        html += `</div>`;
      }
      return html;
    };

    this.node.innerHTML = `
      <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px;">
        <div>
          <span style="font-weight: 700; font-size: 13px;">${this.escape(obj.name)}</span>
          <span style="margin-left: 6px; background: var(--theia-badge-background); color: var(--theia-badge-foreground); border-radius: 3px; padding: 1px 5px; font-size: 10px;">${this.escape(obj.type)}</span>
        </div>
        <button id="horo-inspector-refresh"
          style="background: var(--theia-button-secondaryBackground); color: var(--theia-button-secondaryForeground); border: 1px solid var(--theia-button-border, transparent); padding: 2px 8px; cursor: pointer; font-size: 11px;">
          ↻ Refresh
        </button>
      </div>

      ${sectionHeader('Position')}
      ${vec3Row('position', p.x, p.y, p.z)}

      ${sectionHeader('Rotation')}
      ${vec3Row('rotation', r.x, r.y, r.z)}

      ${sectionHeader('Scale')}
      ${vec3Row('scale', s.x, s.y, s.z)}

      ${componentsHtml()}

      <div style="margin-top: 16px;">
        <button id="horo-inspector-apply"
          style="background: var(--theia-button-background); color: var(--theia-button-foreground); border: none; padding: 4px 14px; cursor: pointer; font-size: 12px;">
          Apply
        </button>
      </div>`;

    this.node.querySelector<HTMLButtonElement>('#horo-inspector-apply')
      ?.addEventListener('click', () => this.applyChanges());

    this.node.querySelector<HTMLButtonElement>('#horo-inspector-refresh')
      ?.addEventListener('click', () => {
        if (this.currentObject) this.showObject(this.currentObject.id);
      });
  }

  private async applyChanges(): Promise<void> {
    if (!this.currentObject) return;

    const get = (key: string): number => {
      const el = this.node.querySelector<HTMLInputElement>(`input[data-key="${key}"]`);
      return parseFloat(el?.value ?? '0');
    };

    const patch: Partial<McpObject> = {
      position: { x: get('position.x'), y: get('position.y'), z: get('position.z') },
      rotation: { x: get('rotation.x'), y: get('rotation.y'), z: get('rotation.z') },
      scale:    { x: get('scale.x'),    y: get('scale.y'),    z: get('scale.z') },
    };

    const applyBtn = this.node.querySelector<HTMLButtonElement>('#horo-inspector-apply');

    const flash = (text: string, bg: string) => {
      if (!applyBtn) return;
      applyBtn.textContent = text;
      applyBtn.style.background = bg;
      applyBtn.style.color = '#fff';
      setTimeout(() => {
        applyBtn.textContent = 'Apply';
        applyBtn.style.background = 'var(--theia-button-background)';
        applyBtn.style.color = 'var(--theia-button-foreground)';
      }, 1500);
    };

    try {
      await this.mcp.updateObject(this.currentObject.id, patch);
      flash('✓ Applied', 'var(--theia-notificationsSuccessIcon-foreground, #73c991)');
    } catch {
      flash('✗ Failed', 'var(--theia-editorError-foreground, #f14c4c)');
    }
  }

  private escape(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
}
