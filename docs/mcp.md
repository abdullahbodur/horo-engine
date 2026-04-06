# Built-in MCP

Horo Engine includes a built-in HTTP MCP server for the editor.

## Overview

- Endpoint: `http://127.0.0.1:39281/mcp`
- Transport: HTTP only
- Bind address: loopback only
- Startup: when the editor is open and `MCP enabled` is checked in `File -> Settings...`

User settings are stored in `~/.horo/settings.json` on macOS/Linux and `%USERPROFILE%\\.horo\\settings.json` on Windows.

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

## Editor Surface

- `File -> Settings...` manages enablement, port, and restart behavior.
- The bottom `MCP` tab shows runtime status, success/failure counts, top tool/resource, and last request time.
- The `MCP` tab includes dedicated `Codex`, `Claude`, and `VS Code` cards with copy-ready config snippets.
- The `MCP` tab includes a live request table with timestamps, duration, request preview, response preview, and error details.
- The `MCP` tab includes a capability catalog so you can inspect the currently exposed tools and resources.
- The server automatically stops when the editor closes.

## Resources

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

These resources are compact by default and intentionally avoid full-scene dumps.

## Tools

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

## Claude Code

Claude Code supports HTTP MCP servers via JSON config. See the official docs:

- https://code.claude.com/docs/en/settings
- https://code.claude.com/docs/en/mcp

Example config:

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

Use that structure in project `.mcp.json` or your user-scoped Claude MCP configuration, depending on how you want to share it.

## Codex

Codex supports HTTP MCP servers via `config.toml`. See the official docs:

- https://developers.openai.com/codex/mcp

Example config:

```toml
[mcp_servers.horo_engine]
url = "http://127.0.0.1:39281/mcp"
```

Use either `~/.codex/config.toml` or a trusted project-scoped `.codex/config.toml`.

## VS Code

Official docs:

- https://code.visualstudio.com/docs/copilot/customization/mcp-servers
- https://code.visualstudio.com/docs/copilot/reference/mcp-configuration

VS Code supports MCP natively and stores server definitions in `mcp.json`.

### Workspace setup

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

### User setup

1. Open the Command Palette.
2. Run `MCP: Open User Configuration`.
3. Add the same server entry to the user `mcp.json`.
4. Save and restart the server if prompted.

### Guided setup

1. Run `MCP: Add Server`.
2. Choose `Workspace` or `Global`.
3. Choose `HTTP`.
4. Enter `http://127.0.0.1:39281/mcp`.
5. Trust and start the server.

### CLI setup

```bash
code --add-mcp "{\"name\":\"horoEngine\",\"type\":\"http\",\"url\":\"http://127.0.0.1:39281/mcp\"}"
```

### Notes

- Workspace MCP config lives in `.vscode/mcp.json`
- User MCP config lives in profile-scoped `mcp.json`
- If Horo's port changes, update the VS Code MCP config and restart the server

## Token-minimal usage guidance

- Prefer `scene.summary` before calling object-level tools.
- Use `editor.search` with a narrow `query` and `limit`.
- Use `editor.get_object` only for the specific object you need.
- Prefer `console.recent` over asking for long log history.
- Mutate with targeted tools instead of requesting broad state dumps first.
