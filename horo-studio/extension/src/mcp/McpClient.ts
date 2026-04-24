/**
 * McpClient — typed HTTP client for Horo Engine's MCP server.
 *
 * All calls go to http://localhost:39281/mcp (or a configured endpoint).
 * The engine's MCP surface is documented in mcp/README.md.
 */

import { injectable } from '@theia/core/shared/inversify';

export interface McpObject {
  id: string;
  name: string;
  type: string;
  parentId?: string;
  children?: McpObject[];
  components?: Record<string, unknown>;
  position?: { x: number; y: number; z: number };
  rotation?: { x: number; y: number; z: number };
  scale?: { x: number; y: number; z: number };
}

export interface McpAsset {
  id: string;
  /** Primary display name (some MCP tools return 'name', others 'displayName'). */
  name: string;
  displayName?: string;
  type: string;
  meshPath?: string;
  albedoPath?: string;
}

export interface McpSceneSummary {
  sceneFile: string;
  objectCount: number;
  assetCount: number;
  isDirty: boolean;
  hasActiveProject: boolean;
  projectPath: string;
  projectName: string;
}

export interface McpToolResult {
  ok: boolean;
  error?: string;
  data?: unknown;
}

@injectable()
export class McpClient {
  private readonly endpoint = 'http://127.0.0.1:39281/mcp';

  constructor() {}

  // ---- Connectivity --------------------------------------------------------

  async ping(): Promise<boolean> {
    try {
      const result = await this.readResource('scene.summary');
      return result !== null;
    } catch {
      return false;
    }
  }

  // ---- Resource reads ------------------------------------------------------

  async readResource(resourceUri: string, extraParams?: Record<string, unknown>): Promise<unknown> {
    const body = {
      jsonrpc: '2.0',
      id: this.nextId(),
      method: 'resources/read',
      params: { uri: resourceUri, ...extraParams },
    };
    const resp = await this.post(body);
    const result = resp?.['result'] as Record<string, unknown> | undefined;
    const contents = result?.['contents'];
    if (Array.isArray(contents) && contents.length > 0) {
      return (contents[0] as Record<string, unknown>)?.['text'] ?? null;
    }
    return null;
  }

  async getSceneSummary(): Promise<McpSceneSummary | null> {
    try {
      const text = await this.readResource('scene.summary');
      if (typeof text === 'string') {
        return JSON.parse(text) as McpSceneSummary;
      }
    } catch { /* fall through */ }
    return null;
  }

  // ---- Scene hierarchy -----------------------------------------------------

  async listObjects(query?: string, limit = 200): Promise<McpObject[]> {
    const args: Record<string, unknown> = { limit };
    if (query) args['query'] = query;
    const result = await this.callTool('editor.list_objects', args);
    if (Array.isArray(result)) return result as McpObject[];
    return [];
  }

  async getObject(id: string): Promise<McpObject | null> {
    const result = await this.callTool('editor.get_object', { id });
    return (result as McpObject) ?? null;
  }

  async getObjectChildren(parentId: string): Promise<McpObject[]> {
    const result = await this.callTool('editor.get_object_children', { id: parentId });
    if (Array.isArray(result)) return result as McpObject[];
    return [];
  }

  async createObject(type: string, parentId?: string): Promise<McpToolResult> {
    const args: Record<string, unknown> = { type };
    if (parentId) args['parentId'] = parentId;
    return this.callToolResult('editor.create_object', args);
  }

  async updateObject(id: string, patch: Partial<McpObject>): Promise<McpToolResult> {
    return this.callToolResult('editor.update_object', { id, ...patch });
  }

  async deleteObject(id: string): Promise<McpToolResult> {
    return this.callToolResult('editor.delete', { id, mode: 'apply' });
  }

  async selectObjects(ids: string[]): Promise<McpToolResult> {
    return this.callToolResult('editor.select', { ids });
  }

  async renameObject(id: string, name: string): Promise<McpToolResult> {
    return this.callToolResult('editor.rename_object', { id, name });
  }

  // ---- Assets --------------------------------------------------------------

  async listAssets(query?: string, limit = 200): Promise<McpAsset[]> {
    const args: Record<string, unknown> = { limit };
    if (query) args['query'] = query;
    const result = await this.callTool('editor.list_assets', args);
    if (Array.isArray(result)) return result as McpAsset[];
    return [];
  }

  async getAsset(id: string): Promise<McpAsset | null> {
    const result = await this.callTool('editor.get_asset', { id });
    return (result as McpAsset) ?? null;
  }

  async selectAsset(id: string): Promise<McpToolResult> {
    return this.callToolResult('editor.select_asset', { id });
  }

  // ---- Scene lifecycle -----------------------------------------------------

  async newScene(): Promise<McpToolResult> {
    return this.callToolResult('editor.new_scene', { mode: 'apply' });
  }

  async saveScene(): Promise<McpToolResult> {
    return this.callToolResult('editor.save_scene', {});
  }

  async reloadScene(): Promise<McpToolResult> {
    return this.callToolResult('editor.reload_scene', { mode: 'apply' });
  }

  // ---- Project management --------------------------------------------------

  async getProjectStatus(): Promise<{ hasProject: boolean; projectPath: string; projectName: string }> {
    try {
      const data = await this.callTool('launcher.get_project_status', {});
      return (data as { hasProject: boolean; projectPath: string; projectName: string }) ??
        { hasProject: false, projectPath: '', projectName: '' };
    } catch {
      return { hasProject: false, projectPath: '', projectName: '' };
    }
  }

  async openProject(projectPath: string): Promise<McpToolResult> {
    return this.callToolResult('launcher.open_project', { path: projectPath });
  }

  async createProject(name: string, projectPath: string): Promise<McpToolResult> {
    return this.callToolResult('launcher.create_project', { name, path: projectPath });
  }

  async closeProject(): Promise<McpToolResult> {
    return this.callToolResult('launcher.close_project', {});
  }

  // ---- Camera --------------------------------------------------------------

  /**
   * Send camera pose to the engine editor camera.
   *
   * The engine does not currently expose a camera MCP tool, so this is a
   * no-op stub. It is here so GameViewWidget can call it unconditionally and
   * the method will start working once the engine adds the tool.
   */
  async setCamera(
    _pos: { x: number; y: number; z: number },
    _yaw: number,
    _pitch: number
  ): Promise<McpToolResult> {
    return { ok: true };
  }

  // ---- Console -------------------------------------------------------------

  async getConsoleLogs(limit = 100): Promise<string[]> {
    try {
      const text = await this.readResource('console.recent', { limit });
      if (typeof text === 'string') {
        const parsed = JSON.parse(text) as Record<string, unknown>;
        // Engine returns { lines: [{time, level, message}], lineCount, hasMore }
        const lines = Array.isArray(parsed?.['lines']) ? parsed['lines'] as Record<string, string>[] : [];
        return lines.map((l) => `[${l['time'] ?? ''}] [${l['level'] ?? 'INFO'}] ${l['message'] ?? ''}`);
      }
    } catch { /* fall through */ }
    return [];
  }

  // ---- Private helpers -----------------------------------------------------

  private idCounter = 1;
  private nextId(): number {
    return this.idCounter++;
  }

  private async post(body: unknown): Promise<Record<string, unknown>> {
    const response = await fetch(this.endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    if (!response.ok) {
      throw new Error(`MCP HTTP ${response.status}: ${response.statusText}`);
    }
    return response.json() as Promise<Record<string, unknown>>;
  }

  private async callTool(toolName: string, args: Record<string, unknown>): Promise<unknown> {
    // Tools can be called with dotted names; the engine also accepts underscored aliases
    const body = {
      jsonrpc: '2.0',
      id: this.nextId(),
      method: 'tools/call',
      params: { name: toolName, arguments: args },
    };
    const resp = await this.post(body);
    // The engine returns results in content[0].text as JSON
    const result = resp?.['result'] as Record<string, unknown> | undefined;
    const contents = result?.['content'];
    if (Array.isArray(contents) && contents.length > 0) {
      const text = (contents[0] as Record<string, unknown>)?.['text'];
      if (typeof text === 'string') {
        try { return JSON.parse(text); } catch { return text; }
      }
    }
    return null;
  }

  private async callToolResult(
    toolName: string,
    args: Record<string, unknown>
  ): Promise<McpToolResult> {
    try {
      const data = await this.callTool(toolName, args);
      return { ok: true, data };
    } catch (err) {
      return { ok: false, error: String(err) };
    }
  }
}
