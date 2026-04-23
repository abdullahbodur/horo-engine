/**
 * McpClient — typed HTTP client for Horo Engine's MCP server.
 *
 * All calls go to http://localhost:39281/mcp (or a configured endpoint).
 * The engine's MCP surface is documented in mcp/README.md.
 */

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
}

export interface McpToolResult {
  ok: boolean;
  error?: string;
  data?: unknown;
}

export class McpClient {
  private endpoint: string;

  constructor(endpoint = 'http://127.0.0.1:39281/mcp') {
    this.endpoint = endpoint;
  }

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

  async readResource(resourceUri: string): Promise<unknown> {
    const body = {
      jsonrpc: '2.0',
      id: this.nextId(),
      method: 'resources/read',
      params: { uri: resourceUri },
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

  // ---- Console -------------------------------------------------------------

  async getConsoleLogs(limit = 100): Promise<string[]> {
    try {
      const text = await this.readResource('console.recent');
      if (typeof text === 'string') {
        const parsed = JSON.parse(text);
        if (Array.isArray(parsed)) return parsed as string[];
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
