# Submodule Integration for AI-Assisted Development

## The Problem: Black-Box Engines

When you use Unity, Unreal, or any closed-source engine with an AI agent via MCP, the agent can inspect your scene and run editor commands — but it has no visibility into how the engine itself works. The engine is a binary SDK. The AI cannot read `Scene::CreateEntity`, trace through the render pipeline, or understand what physics components are available. It has to guess from documentation, which is often incomplete or stale.

This means:

- AI-generated engine API calls often have wrong signatures or miss required setup
- Debugging requires the AI to reason about engine behavior it cannot see
- Scene edits suggested by the AI may not respect the actual component model
- The AI cannot follow a code path from your gameplay code into engine internals

## How Horo Fixes This

Horo is fully open-source. When you embed it as a submodule, the engine source lives in your repository alongside your game code — every `.h` and `.cpp` file, from `math/` vectors up to `editor/` UI code.

When an AI agent connects through the built-in MCP server, it does two things:

1. **Accesses the editor** via MCP tools (scene inspection, asset queries, commands)
2. **Reads the engine source** directly from the filesystem, just like any other file in your project

This means the AI has the full picture. It reads the actual signatures, the actual pipeline code, the actual component definitions — not a docs approximation.

## Concrete Benefits

### Correct API calls on the first try

The AI has seen `Scene::CreateEntity`'s real signature, parameters, and overloads. It writes engine API calls that compile on the first attempt instead of guessing from outdated docs.

### Trace rendering issues through the real pipeline

When something doesn't render correctly, the AI can follow the draw call path from `EditorLayer` through the renderer facade into the OpenGL backend — all within the same context window. No need to describe the pipeline to it; it reads the code.

### Scene edits that respect the actual component model

The AI reads the ECS component definitions directly. It knows which components exist, what data they carry, and what systems process them. Scene edit suggestions are correct by construction.

### Follow calls from gameplay to engine internals

When your game code calls into the engine, the AI can open the engine source and trace the entire call chain. In a black-box engine, this path is invisible.

## Engineering Side Benefits

Beyond AI, the submodule approach also gives you:

- **Pinned commits** — every game build is tied to an exact engine revision
- **Unified reviews** — engine and game changes reviewed in one PR
- **Unified CI** — engine tests and game tests run as one pipeline
- **Debugger traceability** — step from your gameplay breakpoint into engine internals in the same debugger session
- **Immediate fixes** — no waiting for a vendor release to fix an engine bug
