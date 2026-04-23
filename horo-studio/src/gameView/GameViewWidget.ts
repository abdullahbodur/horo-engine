import { injectable, inject, postConstruct } from '@theia/core/shared/inversify';
import { BaseWidget, Message } from '@theia/core/lib/browser';
import { McpClient } from '../mcp/McpClient';

/**
 * GameViewWidget — renders the engine's 3D viewport as a live stream.
 *
 * The engine broadcasts JPEG frames over WebSocket on ws://localhost:39282.
 * Each frame is received as a binary Blob, decoded via createImageBitmap(),
 * and drawn to an HTML5 canvas at up to 30fps.
 *
 * Fly camera controls (client-side):
 *   Right-click drag → look (yaw / pitch)
 *   W / S             → move forward / backward
 *   A / D             → strafe left / right
 *   Q / E             → move down / up
 */
@injectable()
export class GameViewWidget extends BaseWidget {
  static readonly ID = 'horo-game-view';
  static readonly LABEL = 'Game View';

  constructor(@inject(McpClient) private readonly mcp: McpClient) {
    super();
  }

  private canvas: HTMLCanvasElement | null = null;
  private ctx: CanvasRenderingContext2D | null = null;
  private ws: WebSocket | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private animFrameId: number | null = null;
  private pendingFrame: ImageBitmap | null = null;
  private hudOverlay: HTMLDivElement | null = null;

  // ---- Camera state --------------------------------------------------------

  private camPos = { x: 0, y: 2, z: 5 };
  private camYaw = 0;
  private camPitch = -15;
  private readonly keysDown = new Set<string>();
  private isDragging = false;
  private lastMouseX = 0;
  private lastMouseY = 0;

  private readonly MOVE_SPEED = 0.05;
  private readonly LOOK_SPEED = 0.25;

  // ---- Bound event listeners (stored for cleanup) --------------------------

  private readonly onKeyDown = (e: KeyboardEvent) => {
    this.keysDown.add(e.code);
  };

  private readonly onKeyUp = (e: KeyboardEvent) => {
    this.keysDown.delete(e.code);
  };

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
    this.node.style.position = 'relative';
  }

  protected onAfterAttach(msg: Message): void {
    super.onAfterAttach(msg);
    this.buildCanvas();
    this.buildHud();
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
    this.connect();
    this.scheduleRender();
  }

  protected override onBeforeDetach(msg: Message): void {
    super.onBeforeDetach(msg);
    window.removeEventListener('keydown', this.onKeyDown);
    window.removeEventListener('keyup', this.onKeyUp);
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
    this.canvas.setAttribute('tabindex', '0');
    this.node.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d');
    this.resizeCanvas();

    // Resize observer keeps canvas pixel dimensions in sync with widget size
    const ro = new ResizeObserver(() => this.resizeCanvas());
    ro.observe(this.node);

    // Focus on click so keyboard events reach us
    this.canvas.addEventListener('click', () => this.canvas?.focus());

    // Right-click drag → look
    this.canvas.addEventListener('mousedown', (e: MouseEvent) => {
      if (e.button === 2) {
        this.isDragging = true;
        this.lastMouseX = e.clientX;
        this.lastMouseY = e.clientY;
      }
    });

    this.canvas.addEventListener('mousemove', (e: MouseEvent) => {
      if (!this.isDragging) return;
      const dx = e.clientX - this.lastMouseX;
      const dy = e.clientY - this.lastMouseY;
      this.lastMouseX = e.clientX;
      this.lastMouseY = e.clientY;
      this.camYaw -= dx * this.LOOK_SPEED;
      this.camPitch = Math.max(-89, Math.min(89, this.camPitch - dy * this.LOOK_SPEED));
    });

    this.canvas.addEventListener('mouseup', (e: MouseEvent) => {
      if (e.button === 2) this.isDragging = false;
    });

    this.canvas.addEventListener('mouseleave', () => {
      this.isDragging = false;
    });

    this.canvas.addEventListener('contextmenu', (e: Event) => {
      e.preventDefault();
    });
  }

  private resizeCanvas(): void {
    if (!this.canvas) return;
    const rect = this.node.getBoundingClientRect();
    if (rect.width > 0 && rect.height > 0) {
      this.canvas.width = Math.round(rect.width);
      this.canvas.height = Math.round(rect.height);
    }
  }

  // ---- HUD overlay ---------------------------------------------------------

  private buildHud(): void {
    this.hudOverlay = document.createElement('div');
    Object.assign(this.hudOverlay.style, {
      position: 'absolute',
      bottom: '8px',
      left: '8px',
      padding: '6px 10px',
      background: 'rgba(0,0,0,0.55)',
      color: '#ccc',
      fontFamily: 'monospace',
      fontSize: '11px',
      lineHeight: '1.6',
      borderRadius: '4px',
      pointerEvents: 'none',
      userSelect: 'none',
      whiteSpace: 'pre',
    });
    this.node.appendChild(this.hudOverlay);
    this.updateHud();
  }

  private updateHud(): void {
    if (!this.hudOverlay) return;
    const { x, y, z } = this.camPos;
    this.hudOverlay.textContent =
      `[Right-drag: Look]  [WASD: Move]  [Q/E: Down/Up]\n` +
      `Camera  x=${x.toFixed(1)}  y=${y.toFixed(1)}  z=${z.toFixed(1)}` +
      `   yaw=${this.camYaw.toFixed(1)}°  pitch=${this.camPitch.toFixed(1)}°`;
  }

  // ---- Camera movement -----------------------------------------------------

  private readonly MOVE_KEYS = new Set([
    'KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE',
  ]);

  private applyMovement(): void {
    const active = [...this.keysDown].some((k) => this.MOVE_KEYS.has(k));
    if (!active) return;

    const yawRad = (this.camYaw * Math.PI) / 180;
    // Forward direction projected onto the XZ plane (ignores pitch for WASD)
    const fwdX = -Math.sin(yawRad);
    const fwdZ = -Math.cos(yawRad);
    const rightX = Math.cos(yawRad);
    const rightZ = -Math.sin(yawRad);

    const spd = this.MOVE_SPEED;

    if (this.keysDown.has('KeyW')) { this.camPos.x += fwdX * spd; this.camPos.z += fwdZ * spd; }
    if (this.keysDown.has('KeyS')) { this.camPos.x -= fwdX * spd; this.camPos.z -= fwdZ * spd; }
    if (this.keysDown.has('KeyA')) { this.camPos.x -= rightX * spd; this.camPos.z -= rightZ * spd; }
    if (this.keysDown.has('KeyD')) { this.camPos.x += rightX * spd; this.camPos.z += rightZ * spd; }
    if (this.keysDown.has('KeyQ')) { this.camPos.y -= spd; }
    if (this.keysDown.has('KeyE')) { this.camPos.y += spd; }

    this.sendCameraUpdate();
  }

  private sendCameraUpdate(): void {
    // Fire-and-forget — engine may not have a camera MCP tool yet (stub returns ok immediately).
    this.mcp.setCamera(this.camPos, this.camYaw, this.camPitch).catch(() => { /* ignore */ });
    this.updateHud();
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

    this.applyMovement();

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
