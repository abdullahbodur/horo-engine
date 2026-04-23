import { injectable, postConstruct, preDestroy } from '@theia/core/shared/inversify';
import { BaseWidget, Message } from '@theia/core/lib/browser';

/**
 * GameViewWidget — renders the engine's 3D viewport as a live stream.
 *
 * The engine broadcasts JPEG frames over WebSocket on ws://localhost:39282.
 * Each frame is received as a binary Blob, decoded via createImageBitmap(),
 * and drawn to an HTML5 canvas at up to 30fps.
 */
@injectable()
export class GameViewWidget extends BaseWidget {
  static readonly ID = 'horo-game-view';
  static readonly LABEL = 'Game View';

  private canvas: HTMLCanvasElement | null = null;
  private ctx: CanvasRenderingContext2D | null = null;
  private ws: WebSocket | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private animFrameId: number | null = null;
  private pendingFrame: ImageBitmap | null = null;

  private readonly WS_URL = 'ws://127.0.0.1:39282';
  private readonly RECONNECT_MS = 2000;

  @postConstruct()
  protected init(): void {
    this.id = GameViewWidget.ID;
    this.title.label = GameViewWidget.LABEL;
    this.title.caption = 'Horo Engine — Game View';
    this.title.closable = true;
    this.title.iconClass = 'codicon codicon-play-circle';
    this.addClass('horo-game-view-widget');
    this.node.style.background = '#1a1a1a';
    this.node.style.display = 'flex';
    this.node.style.flexDirection = 'column';
    this.node.style.overflow = 'hidden';
  }

  protected onAfterAttach(msg: Message): void {
    super.onAfterAttach(msg);
    this.buildCanvas();
    this.connect();
    this.scheduleRender();
  }

  @preDestroy()
  protected onBeforeDetach(msg: Message): void {
    super.onBeforeDetach(msg);
    this.disconnect();
    if (this.animFrameId !== null) {
      cancelAnimationFrame(this.animFrameId);
      this.animFrameId = null;
    }
  }

  protected onResize(_msg: Message): void {
    this.resizeCanvas();
  }

  // ---- Canvas setup --------------------------------------------------------

  private buildCanvas(): void {
    this.canvas = document.createElement('canvas');
    this.canvas.style.width = '100%';
    this.canvas.style.height = '100%';
    this.canvas.style.display = 'block';
    this.canvas.style.imageRendering = 'crisp-edges';
    this.node.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d');
    this.resizeCanvas();

    // Resize observer keeps canvas pixel dimensions in sync with widget size
    const ro = new ResizeObserver(() => this.resizeCanvas());
    ro.observe(this.node);
  }

  private resizeCanvas(): void {
    if (!this.canvas) return;
    const rect = this.node.getBoundingClientRect();
    if (rect.width > 0 && rect.height > 0) {
      this.canvas.width = Math.round(rect.width);
      this.canvas.height = Math.round(rect.height);
    }
  }

  // ---- WebSocket connection -------------------------------------------------

  private connect(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }

    try {
      this.ws = new WebSocket(this.WS_URL);
      this.ws.binaryType = 'blob';

      this.ws.onopen = () => {
        console.log('[GameView] Connected to engine framebuffer stream');
        this.drawStatusText('Connected — waiting for frames…');
      };

      this.ws.onmessage = (event: MessageEvent) => {
        if (event.data instanceof Blob) {
          createImageBitmap(event.data).then((bitmap) => {
            if (this.pendingFrame) this.pendingFrame.close();
            this.pendingFrame = bitmap;
          }).catch(() => { /* decode error — skip frame */ });
        }
      };

      this.ws.onclose = () => {
        console.log('[GameView] Disconnected — will retry in 2s');
        this.drawStatusText('Disconnected — engine not running?');
        this.scheduleReconnect();
      };

      this.ws.onerror = () => {
        this.ws?.close();
      };
    } catch {
      this.scheduleReconnect();
    }
  }

  private disconnect(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      this.ws.onclose = null;
      this.ws.close();
      this.ws = null;
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer !== null) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, this.RECONNECT_MS);
  }

  // ---- Render loop ---------------------------------------------------------

  private scheduleRender(): void {
    const loop = () => {
      this.renderFrame();
      this.animFrameId = requestAnimationFrame(loop);
    };
    this.animFrameId = requestAnimationFrame(loop);
  }

  private renderFrame(): void {
    if (!this.ctx || !this.canvas) return;

    if (this.pendingFrame) {
      const { width, height } = this.canvas;
      this.ctx.drawImage(this.pendingFrame, 0, 0, width, height);
      // Don't close the bitmap — reuse until next frame arrives
    }
  }

  private drawStatusText(text: string): void {
    if (!this.ctx || !this.canvas) return;
    const { width, height } = this.canvas;
    this.ctx.fillStyle = '#1a1a1a';
    this.ctx.fillRect(0, 0, width, height);
    this.ctx.fillStyle = '#666';
    this.ctx.font = '14px monospace';
    this.ctx.textAlign = 'center';
    this.ctx.fillText(text, width / 2, height / 2);
  }
}
