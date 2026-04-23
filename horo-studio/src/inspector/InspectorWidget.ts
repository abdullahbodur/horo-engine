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

    const fields: Array<{ label: string; key: string; value: string }> = [
      { label: 'ID', key: 'id', value: obj.id },
      { label: 'Name', key: 'name', value: obj.name },
      { label: 'Type', key: 'type', value: obj.type },
    ];

    if (obj.position) {
      fields.push(
        { label: 'X', key: 'position.x', value: String(obj.position.x) },
        { label: 'Y', key: 'position.y', value: String(obj.position.y) },
        { label: 'Z', key: 'position.z', value: String(obj.position.z) },
      );
    }

    const sectionHtml = (title: string, rows: typeof fields) => `
      <div style="font-weight: 600; margin: 10px 0 4px; color: var(--theia-settings-headerForeground);">${title}</div>
      <table style="width:100%; border-collapse: collapse;">
        ${rows.map((f) => `
          <tr>
            <td style="padding: 2px 6px 2px 0; color: var(--theia-descriptionForeground); width: 80px;">${f.label}</td>
            <td><input
              data-key="${f.key}"
              style="width:100%; background: var(--theia-input-background); color: var(--theia-input-foreground); border: 1px solid var(--theia-input-border); padding: 2px 4px; font-size: 12px;"
              value="${this.escape(f.value)}"
              ${f.key === 'id' || f.key === 'type' ? 'readonly' : ''}
            /></td>
          </tr>`).join('')}
      </table>`;

    this.node.innerHTML = `
      <div style="font-weight: 700; font-size: 13px; margin-bottom: 8px;">${this.escape(obj.name)}</div>
      ${sectionHtml('Transform', fields.filter((f) => f.key !== 'id'))}
      <div style="margin-top: 10px;">
        <button id="horo-inspector-apply"
          style="background: var(--theia-button-background); color: var(--theia-button-foreground); border: none; padding: 4px 12px; cursor: pointer;">
          Apply
        </button>
      </div>`;

    const applyBtn = this.node.querySelector<HTMLButtonElement>('#horo-inspector-apply');
    applyBtn?.addEventListener('click', () => this.applyChanges());
  }

  private async applyChanges(): Promise<void> {
    if (!this.currentObject) return;

    const inputs = this.node.querySelectorAll<HTMLInputElement>('input[data-key]');
    const patch: Partial<McpObject> = {};

    inputs.forEach((input) => {
      const key = input.dataset['key'] ?? '';
      const val = input.value;
      if (key === 'name') patch.name = val;
      else if (key === 'position.x' && this.currentObject?.position)
        patch.position = { ...this.currentObject.position, x: parseFloat(val) };
      else if (key === 'position.y' && this.currentObject?.position)
        patch.position = { ...this.currentObject.position, y: parseFloat(val) };
      else if (key === 'position.z' && this.currentObject?.position)
        patch.position = { ...this.currentObject.position, z: parseFloat(val) };
    });

    await this.mcp.updateObject(this.currentObject.id, patch);
  }

  private escape(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
}
