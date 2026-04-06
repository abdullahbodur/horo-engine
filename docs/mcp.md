# Built-in MCP

Horo Engine includes a built-in HTTP MCP server for the editor.

## Overview

- Endpoint: `http://127.0.0.1:39281/mcp`
- Transport: HTTP only
- Bind address: loopback only
- Auth: `Authorization: Bearer <token>`
- Startup: when the editor is open and `MCP enabled` is checked in `File -> Settings...`

User settings are stored in `~/.horo/settings.json` on macOS/Linux and `%USERPROFILE%\\.horo\\settings.json` on Windows.

```json
{
  "mcp": {
    "enabled": true,
    "transport": "http",
    "host": "127.0.0.1",
    "port": 39281,
    "authToken": "your-generated-token",
    "autoStart": true
  }
}
```

## Editor Surface

- `File -> Settings...` manages enablement, port, token regeneration, and restart behavior.
- The bottom `MCP` tab shows runtime status, request counts, recent activity, and copy-ready config snippets.
- The server automatically stops when the editor closes.

## Default Resources

- `scene.summary`
- `scene.selection`
- `scene.assets`
- `console.recent`

These resources are compact by default and intentionally avoid full-scene dumps.

## Default Tools

- `editor.search`
- `editor.get_object`
- `editor.select`
- `editor.create_object`
- `editor.update_object`
- `editor.transform`
- `editor.duplicate`
- `editor.delete`
- `editor.save_scene`
- `editor.reload_scene`

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
      "url": "http://127.0.0.1:39281/mcp",
      "headers": {
        "Authorization": "Bearer YOUR_TOKEN"
      }
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

[mcp_servers.horo_engine.http_headers]
Authorization = "Bearer YOUR_TOKEN"
```

Use either `~/.codex/config.toml` or a trusted project-scoped `.codex/config.toml`.

## Token-minimal usage guidance

- Prefer `scene.summary` before calling object-level tools.
- Use `editor.search` with a narrow `query` and `limit`.
- Use `editor.get_object` only for the specific object you need.
- Prefer `console.recent` over asking for long log history.
- Mutate with targeted tools instead of requesting broad state dumps first.
