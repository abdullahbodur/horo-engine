import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { BaseWidget, Message } from '@theia/core/lib/browser';
import { McpClient } from '../mcp/McpClient';

@injectable()
export class ConsoleWidget extends BaseWidget {
  static readonly ID = 'horo-console';
  static readonly LABEL = 'Console';

  private pollTimer?: ReturnType<typeof setInterval>;
  private knownLines = 0;
  private autoScroll = true;

  constructor(@inject(McpClient) private readonly mcp: McpClient) {
    super();
  }

  @postConstruct()
  protected init(): void {
    this.id = ConsoleWidget.ID;
    this.title.label = ConsoleWidget.LABEL;
    this.title.caption = 'Engine Console';
    this.title.iconClass = 'codicon codicon-output';
    this.title.closable = false;
    this.addClass('horo-console-widget');

    this.node.style.display = 'flex';
    this.node.style.flexDirection = 'column';
    this.node.style.height = '100%';
    this.node.style.overflow = 'hidden';
    this.node.style.fontFamily = 'var(--theia-code-font-family, monospace)';
    this.node.style.fontSize = '12px';

    // Toolbar
    const toolbar = document.createElement('div');
    toolbar.style.cssText =
      'display:flex; gap:4px; padding:4px 6px; background:var(--theia-sideBar-background); border-bottom:1px solid var(--theia-widget-border); flex-shrink:0;';

    const clearBtn = document.createElement('button');
    clearBtn.textContent = 'Clear';
    clearBtn.style.cssText =
      'background:var(--theia-button-secondaryBackground); color:var(--theia-button-secondaryForeground); border:none; padding:2px 8px; cursor:pointer; font-size:11px;';
    clearBtn.addEventListener('click', () => this.clearOutput());

    const autoScrollLabel = document.createElement('label');
    autoScrollLabel.style.cssText =
      'display:flex; align-items:center; gap:3px; font-size:11px; color:var(--theia-foreground); cursor:pointer;';
    const autoScrollCheck = document.createElement('input');
    autoScrollCheck.type = 'checkbox';
    autoScrollCheck.checked = true;
    autoScrollCheck.addEventListener('change', () => {
      this.autoScroll = autoScrollCheck.checked;
    });
    autoScrollLabel.appendChild(autoScrollCheck);
    autoScrollLabel.appendChild(document.createTextNode('Auto-scroll'));

    toolbar.appendChild(clearBtn);
    toolbar.appendChild(autoScrollLabel);
    this.node.appendChild(toolbar);

    // Output area
    const output = document.createElement('div');
    output.id = 'horo-console-output';
    output.style.cssText =
      'flex:1; overflow-y:auto; padding:4px 8px; background:var(--theia-terminal-background, #1e1e1e); color:var(--theia-terminal-foreground, #d4d4d4);';
    this.node.appendChild(output);

    this.pollTimer = setInterval(() => this.poll(), 1000);
    this.poll();
  }

  protected override onBeforeDetach(msg: Message): void {
    if (this.pollTimer) clearInterval(this.pollTimer);
    this.pollTimer = undefined;
    super.onBeforeDetach(msg);
  }

  private async poll(): Promise<void> {
    try {
      const lines = await this.mcp.getConsoleLogs(200);
      const output = this.node.querySelector<HTMLDivElement>('#horo-console-output');
      if (!output) return;

      if (lines.length > this.knownLines) {
        const newLines = lines.slice(this.knownLines);
        for (const line of newLines) {
          const el = document.createElement('div');
          el.style.cssText =
            'white-space:pre-wrap; word-break:break-all; line-height:1.5; padding:1px 0;';
          const upper = line.toUpperCase();
          if (upper.includes('[ERROR]') || upper.includes(' ERROR ')) {
            el.style.color = '#f48771';
          } else if (
            upper.includes('[WARN]') ||
            upper.includes(' WARNING ') ||
            upper.includes(' WARN ')
          ) {
            el.style.color = '#cca700';
          } else if (upper.includes('[DEBUG]') || upper.includes(' DEBUG ')) {
            el.style.color = '#7fb0d5';
          } else {
            el.style.color = 'var(--theia-terminal-foreground, #d4d4d4)';
          }
          el.textContent = line;
          output.appendChild(el);
        }
        this.knownLines = lines.length;
        if (this.autoScroll) {
          output.scrollTop = output.scrollHeight;
        }
      }
    } catch {
      // Engine not running — silently skip
    }
  }

  private clearOutput(): void {
    const output = this.node.querySelector<HTMLDivElement>('#horo-console-output');
    if (output) output.innerHTML = '';
    this.knownLines = 0;
  }
}
