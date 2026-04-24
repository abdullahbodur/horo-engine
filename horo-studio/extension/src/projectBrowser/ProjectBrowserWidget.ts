import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { BaseWidget } from '@theia/core/lib/browser';
import { McpClient } from '../mcp/McpClient';

@injectable()
export class ProjectBrowserWidget extends BaseWidget {
  static readonly ID = 'horo-project-browser';
  static readonly LABEL = 'Projects';

  private statusDot: HTMLElement | null = null;
  private statusLabel: HTMLElement | null = null;
  private projectInfo: HTMLElement | null = null;
  private feedbackEl: HTMLElement | null = null;
  private closeBtn: HTMLButtonElement | null = null;

  private openPathInput: HTMLInputElement | null = null;
  private createNameInput: HTMLInputElement | null = null;
  private createPathInput: HTMLInputElement | null = null;

  constructor(
    @inject(McpClient) private readonly mcp: McpClient,
  ) {
    super();
    this.id = ProjectBrowserWidget.ID;
    this.title.label = ProjectBrowserWidget.LABEL;
    this.title.caption = 'Project Browser';
    this.title.iconClass = 'codicon codicon-folder';
    this.title.closable = false;
    this.addClass('horo-project-browser');
    this.node.style.cssText =
      'display:flex;flex-direction:column;height:100%;overflow:hidden;' +
      'background:#1e1e1e;color:#ccc;font-size:13px;';
  }

  @postConstruct()
  protected init(): void {
    this.node.innerHTML = '';

    // Header
    const header = document.createElement('div');
    header.style.cssText =
      'padding:8px;border-bottom:1px solid #333;flex-shrink:0;' +
      'background:var(--theia-sideBar-background);';

    const connRow = document.createElement('div');
    connRow.style.cssText = 'display:flex;align-items:center;gap:6px;margin-bottom:4px;';

    this.statusDot = document.createElement('span');
    this.statusDot.style.cssText =
      'width:8px;height:8px;border-radius:50%;background:#555;display:inline-block;flex-shrink:0;';

    this.statusLabel = document.createElement('span');
    this.statusLabel.style.cssText = 'font-size:11px;color:#888;';
    this.statusLabel.textContent = 'Connecting…';

    connRow.appendChild(this.statusDot);
    connRow.appendChild(this.statusLabel);
    header.appendChild(connRow);

    this.projectInfo = document.createElement('div');
    this.projectInfo.style.cssText = 'font-size:12px;color:#aaa;display:none;';
    header.appendChild(this.projectInfo);

    this.node.appendChild(header);

    // Body
    const body = document.createElement('div');
    body.style.cssText = 'flex:1;overflow:auto;padding:8px;display:flex;flex-direction:column;gap:12px;';

    // --- Open Project section ---
    const openSection = this.makeSection('Open Project');

    this.openPathInput = document.createElement('input');
    this.openPathInput.type = 'text';
    this.openPathInput.placeholder = 'Project path…';
    this.openPathInput.style.cssText = this.inputStyle();

    const openBtn = document.createElement('button');
    openBtn.textContent = 'Open';
    openBtn.style.cssText = this.btnStyle('#0e639c', '#fff');
    openBtn.addEventListener('click', () => this.handleOpenProject());

    openSection.appendChild(this.openPathInput);
    openSection.appendChild(openBtn);

    // --- Create Project section ---
    const createSection = this.makeSection('Create Project');

    this.createNameInput = document.createElement('input');
    this.createNameInput.type = 'text';
    this.createNameInput.placeholder = 'Project name…';
    this.createNameInput.style.cssText = this.inputStyle();

    this.createPathInput = document.createElement('input');
    this.createPathInput.type = 'text';
    this.createPathInput.placeholder = 'Project path…';
    this.createPathInput.style.cssText = this.inputStyle();

    const createBtn = document.createElement('button');
    createBtn.textContent = 'Create';
    createBtn.style.cssText = this.btnStyle('#0e639c', '#fff');
    createBtn.addEventListener('click', () => this.handleCreateProject());

    createSection.appendChild(this.createNameInput);
    createSection.appendChild(this.createPathInput);
    createSection.appendChild(createBtn);

    // --- Close Project button ---
    this.closeBtn = document.createElement('button');
    this.closeBtn.textContent = 'Close Project';
    this.closeBtn.style.cssText = this.btnStyle('#5a1d1d', '#f88');
    this.closeBtn.style.display = 'none';
    this.closeBtn.addEventListener('click', () => this.handleCloseProject());

    // --- Feedback ---
    this.feedbackEl = document.createElement('div');
    this.feedbackEl.style.cssText =
      'font-size:12px;padding:4px 0;min-height:20px;word-break:break-all;';

    body.appendChild(openSection);
    body.appendChild(createSection);
    body.appendChild(this.closeBtn);
    body.appendChild(this.feedbackEl);

    this.node.appendChild(body);

    this.refresh();
    setInterval(() => this.refresh(), 2000);
  }

  async refresh(): Promise<void> {
    try {
      const status = await this.mcp.getProjectStatus();
      this.setConnected(true);

      if (status.hasProject) {
        this.projectInfo!.style.display = 'block';
        this.projectInfo!.textContent = `${status.projectName} — ${status.projectPath}`;
        this.closeBtn!.style.display = 'block';
      } else {
        this.projectInfo!.style.display = 'none';
        this.closeBtn!.style.display = 'none';
      }
    } catch {
      this.setConnected(false);
      this.projectInfo!.style.display = 'none';
      this.closeBtn!.style.display = 'none';
    }
  }

  private setConnected(connected: boolean): void {
    if (!this.statusDot || !this.statusLabel) return;
    this.statusDot.style.background = connected ? '#4caf50' : '#555';
    this.statusLabel.textContent = connected ? 'Engine connected' : 'Engine not connected';
  }

  private async handleOpenProject(): Promise<void> {
    const path = this.openPathInput?.value.trim() ?? '';
    if (!path) { this.setFeedback('Please enter a project path.', true); return; }
    this.setFeedback('Opening…', false);
    const result = await this.mcp.openProject(path);
    if (result.ok) {
      this.setFeedback('Project opened.', false);
      if (this.openPathInput) this.openPathInput.value = '';
      this.refresh();
    } else {
      this.setFeedback(`Error: ${result.error ?? 'unknown error'}`, true);
    }
  }

  private async handleCreateProject(): Promise<void> {
    const name = this.createNameInput?.value.trim() ?? '';
    const path = this.createPathInput?.value.trim() ?? '';
    if (!name) { this.setFeedback('Please enter a project name.', true); return; }
    if (!path) { this.setFeedback('Please enter a project path.', true); return; }
    this.setFeedback('Creating…', false);
    const result = await this.mcp.createProject(name, path);
    if (result.ok) {
      this.setFeedback('Project created and opened.', false);
      if (this.createNameInput) this.createNameInput.value = '';
      if (this.createPathInput) this.createPathInput.value = '';
      this.refresh();
    } else {
      this.setFeedback(`Error: ${result.error ?? 'unknown error'}`, true);
    }
  }

  private async handleCloseProject(): Promise<void> {
    this.setFeedback('Closing…', false);
    const result = await this.mcp.closeProject();
    if (result.ok) {
      this.setFeedback('Project closed.', false);
      this.refresh();
    } else {
      this.setFeedback(`Error: ${result.error ?? 'unknown error'}`, true);
    }
  }

  private setFeedback(msg: string, isError: boolean): void {
    if (!this.feedbackEl) return;
    this.feedbackEl.textContent = msg;
    this.feedbackEl.style.color = isError ? '#f88' : '#8f8';
  }

  private makeSection(title: string): HTMLElement {
    const section = document.createElement('div');
    section.style.cssText = 'display:flex;flex-direction:column;gap:6px;';

    const label = document.createElement('div');
    label.textContent = title;
    label.style.cssText =
      'font-size:11px;font-weight:600;color:#888;text-transform:uppercase;letter-spacing:0.05em;';

    section.appendChild(label);
    return section;
  }

  private inputStyle(): string {
    return (
      'background:#2d2d2d;color:#ccc;border:1px solid #555;border-radius:2px;' +
      'padding:4px 8px;font-size:12px;width:100%;box-sizing:border-box;'
    );
  }

  private btnStyle(bg: string, fg: string): string {
    return (
      `background:${bg};color:${fg};border:none;border-radius:2px;` +
      'padding:5px 12px;font-size:12px;cursor:pointer;align-self:flex-start;'
    );
  }
}
