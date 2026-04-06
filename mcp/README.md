# MCP Module

`mcp/` contains Horo Engine's built-in Model Context Protocol server for the editor.

## What this module does

- Loads and saves user MCP settings from `~/.horo/settings.json`
- Publishes compact editor snapshots for MCP resources and tools
- Hosts a loopback-only HTTP MCP endpoint at `http://127.0.0.1:39281/mcp`
- Queues editor mutations onto the main thread instead of mutating editor state from the network thread

The editor owns lifecycle integration. When `MCP enabled` is turned on in `File -> Settings...`, the server starts automatically while the editor is open and stops when the editor closes.

## Files

- `McpSettings.*`: settings path resolution and JSON persistence
- `McpSnapshot.*`: compact editor snapshot models and summary/search builders
- `McpProtocol.*`: MCP request handling, resources, tools, telemetry
- `McpServer.*`: minimal HTTP transport for localhost MCP traffic
- `McpController.*`: runtime coordination, status tracking, request history, command queue, editor bridge

## Runtime model

1. `EditorLayer` initializes `McpController`.
2. The editor publishes a compact immutable snapshot every frame.
3. HTTP requests hit `McpProtocol`.
4. Read requests are served from the latest snapshot.
5. Write requests become queued commands.
6. `EditorLayer::OnUpdate()` drains that queue on the main thread and applies the mutation safely.

## MCP surface

Resources:

- `scene.summary`
- `scene.selection`
- `scene.assets`
- `scene.hierarchy`
- `scene.objects`
- `scene.scene_status`
- `assets.selection`
- `assets.catalog`
- `console.recent`
- `console.summary`

Tools:

- `editor.search`
- `editor.get_object`
- `editor.list_objects`
- `editor.get_objects`
- `editor.get_object_children`
- `editor.get_object_parent`
- `editor.count_objects`
- `editor.select`
- `editor.clear_selection`
- `editor.create_object`
- `editor.create_object_from_asset`
- `editor.update_object`
- `editor.transform`
- `editor.rename_object`
- `editor.reparent_object`
- `editor.duplicate`
- `editor.delete`
- `editor.list_assets`
- `editor.get_asset`
- `editor.search_assets`
- `editor.count_assets`
- `editor.select_asset`
- `editor.update_asset`
- `editor.delete_asset`
- `editor.scene_status`
- `editor.get_scene_file`
- `editor.new_scene`
- `editor.save_scene`
- `editor.reload_scene`
- `editor.search_console`

The design is intentionally summary-first to keep token usage low.

## MCP tab

The bottom `MCP` tab is also the built-in debugger for the server:

- Status strip for endpoint, request totals, success/failure counts, active requests, top tool/resource, and last request time
- `Codex`, `Claude`, and `VS Code` cards with copy-ready config snippets and the expected config file locations
- Live request history with timestamps, duration, method/tool name, request preview, response preview, HTTP status, and errors
- Capability catalog listing the current built-in tools and resources

## Settings

Horo stores user MCP settings in:

- Windows: `%USERPROFILE%\\.horo\\settings.json`
- macOS/Linux: `~/.horo/settings.json`

Example:

```json
{
  "mcp": {
    "enabled": true,
    "transport": "http",
    "host": "127.0.0.1",
    "port": 39281,
    "autoStart": true
  }
}
```

`host` is intentionally pinned to `127.0.0.1`. The server does not expose itself on non-loopback interfaces.

## Claude Code integration

Official docs:

- https://code.claude.com/docs/en/mcp
- https://code.claude.com/docs/en/settings

Anthropic documents HTTP MCP as a supported transport, and its settings docs show that user/local MCP configuration lives in `~/.claude.json`, while project-scoped MCP config can live in `.mcp.json`.

### Option 1: use Claude CLI

```bash
claude mcp add --transport http horo-engine http://127.0.0.1:39281/mcp
```

Useful follow-ups:

```bash
claude mcp list
claude mcp get horo-engine
```

### Option 2: project config

Add a `.mcp.json` file to your project:

```json
{
  "mcpServers": {
    "horo-engine": {
      "type": "http",
      "url": "http://127.0.0.1:39281/mcp"
    }
  }
}
```

### Option 3: user config

Claude also stores user/local MCP server configuration in `~/.claude.json`. If you prefer a user-scoped setup instead of a project file, add the same MCP server definition there.

## Codex integration

Official docs:

- https://developers.openai.com/codex/mcp

OpenAI documents Codex MCP configuration in `config.toml`, usually at `~/.codex/config.toml`, with optional project-scoped `.codex/config.toml`.

### Option 1: edit `config.toml`

```toml
[mcp_servers.horo_engine]
url = "http://127.0.0.1:39281/mcp"
```

### Option 2: use Codex CLI

The Codex docs also describe `codex mcp` for managing servers interactively:

```bash
codex mcp --help
```

After configuration, `/mcp` in the Codex TUI shows active MCP servers.

## VS Code integration

Official docs:

- https://code.visualstudio.com/docs/copilot/customization/mcp-servers
- https://code.visualstudio.com/docs/copilot/reference/mcp-configuration

VS Code supports MCP natively and uses `mcp.json` for server configuration. Microsoft documents workspace scope, user-profile scope, guided setup from the Command Palette, and CLI setup.

### Option 1: workspace setup with `.vscode/mcp.json`

Create `.vscode/mcp.json`:

```json
{
  "servers": {
    "horoEngine": {
      "type": "http",
      "url": "http://127.0.0.1:39281/mcp"
    }
  }
}
```

This is the best option when you want the MCP config to live with the project and be shareable with the team.

### Option 2: user profile setup

1. Open the Command Palette.
2. Run `MCP: Open User Configuration`.
3. Add the same `horoEngine` server entry to the user `mcp.json`.
4. Save the file.
5. Start or restart the server when prompted.

### Option 3: guided setup in the UI

1. Open the Command Palette.
2. Run `MCP: Add Server`.
3. Choose `Workspace` or `Global`.
4. Choose `HTTP`.
5. Enter `http://127.0.0.1:39281/mcp`.
6. Trust and start the server.

### CLI setup

VS Code also supports adding MCP servers from the command line:

```bash
code --add-mcp "{\"name\":\"horoEngine\",\"type\":\"http\",\"url\":\"http://127.0.0.1:39281/mcp\"}"
```

### Notes for VS Code

- Workspace MCP config lives in `.vscode/mcp.json`
- User MCP config lives in profile-scoped `mcp.json`
- VS Code asks you to trust newly added MCP servers before starting them
- If Horo's port changes, update the VS Code MCP config and restart the server

## Token-minimal usage tips

- Ask for `scene.summary` first instead of broad object dumps.
- Use `editor.search` with a small `limit`.
- Call `editor.get_object` only for the object you actually need.
- Prefer `console.recent` over long log history.
- Use targeted mutation tools directly instead of first requesting the whole scene.

## Related docs

- [docs/mcp.md](../docs/mcp.md)
