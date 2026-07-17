# Horo Engine

> [!WARNING]
> Horo Engine is under active development.
> The repository does not currently provide a stable public release, production
> SDK, or ready-to-use editor build. APIs, module boundaries, and project layout
> may change without backward compatibility guarantees until the first stable
> release line is established.

Horo Engine is an open-source C++20 game engine and AI-centric editor IDE.

The current focus is building the engine around a clean modular architecture,
explicit ownership boundaries, deterministic tooling, and editor-native AI
workflows that reason from engine data instead of treating the editor as a black
box.

## Repository Layout Target

The active repository is being organized around this high-level structure:

```text
horo-engine/
├── include/Horo/      # public engine API
├── src/               # engine implementation
├── apps/              # editor and CLI application entry points
├── sdk/               # SDK templates, schemas, and integration files
├── examples/          # sample projects, packages, and extensions
├── tests/             # unit, integration, UI, and smoke tests
├── scripts/           # repository automation scripts
├── tools/             # standalone developer tools
├── cmake/             # CMake modules
├── docs/              # architecture and design documentation
└── vendor/            # third-party dependency location
```

See [Desired Project Tree](./docs/architecture/desired-project-tree.md) for the
full target structure.

## Build Status

The active root `CMakeLists.txt` is currently a minimal skeleton used to validate
the new layout shell.

```bash
cmake -S . -B build/skeleton -DBUILD_TESTING=ON
```

Full editor/runtime build commands will be restored as modules are introduced
into the active layout.

## AI-Centric Editor Direction

Horo's editor architecture is designed for agents that operate close to the
engine rather than guessing from screenshots. The intended AI model is
numeric-first:

- agents query typed engine/editor state through MCP
- transforms, bounds, raycasts, contacts, animation curves, and telemetry are
  preferred over raw visual data
- visual captures are secondary and should be annotated with entity IDs and
  measurements
- viewport inline chat and magic AI tools should execute through typed,
  undoable editor commands

See:

- [Editor AI Agent Architecture](./docs/architecture/editor/editor-ai-agent-architecture.md)
- [MCP Architecture](./docs/architecture/interfaces/mcp-architecture.md)
- [Desired Project Tree](./docs/architecture/desired-project-tree.md)

## Documentation

| Area                         | Document                                                                                                               |
|------------------------------|------------------------------------------------------------------------------------------------------------------------|
| Architecture index           | [docs/architecture/README.md](./docs/architecture/README.md)                                                           |
| Desired repository layout    | [docs/architecture/desired-project-tree.md](./docs/architecture/desired-project-tree.md)                               |
| Editor AI agent architecture | [docs/architecture/editor/editor-ai-agent-architecture.md](./docs/architecture/editor/editor-ai-agent-architecture.md) |
| Release architecture         | [docs/architecture/release/release.md](./docs/architecture/release/release.md)                                         |
| CLI architecture             | [docs/architecture/interfaces/cli-architecture.md](./docs/architecture/interfaces/cli-architecture.md)                 |
| MCP architecture             | [docs/architecture/interfaces/mcp-architecture.md](./docs/architecture/interfaces/mcp-architecture.md)                 |
| ADRs                         | [docs/adr/README.md](./docs/adr/README.md)                                                                             |

### Topics needs to be covered

| Topics                                                                                | Notes |
|---------------------------------------------------------------------------------------|-------|
| We need to create an example module with a panel                                      |       |
| We should prepare an agent mode with vierport that shows real agents as camera on gui |       |

## License

[MIT](LICENSE).
