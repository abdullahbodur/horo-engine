# Local C/C++ Analysis with SonarQube MCP and VS Code

## Purpose

Use an AI coding agent to analyze changed C/C++ files through SonarQube MCP and
an open VS Code workspace with SonarQube for IDE installed. Connected mode applies the project's supported
Quality Profile rules locally. This workflow does not run SonarScanner or upload
an analysis report; it does not replace CI analysis or a server Quality Gate.

```text
AI coding agent -> SonarQube MCP -> VS Code + SonarQube for IDE -> local source files
                                         |
                                         +-> connected project rules/settings
```

## Prerequisites

- A running Docker daemon and an MCP-capable coding agent.
- VS Code with SonarQube for IDE installed and the repository folder open.
- A current compilation database for the intended CMake build configuration, with
  supported compiler information for the target translation units.
- A SonarQube Cloud (SonarCloud) or Server connection, a user token with access to
  the project, and IDE connected-mode binding to that exact project.
- The IDE bridge port, normally in the 64120–64130 range. Use the port reported by
  the IDE; do not assume every running IDE uses 64120.

Examples use `<organization-key>`, `<project-key>`, and
`/absolute/path/to/project` as placeholders. Replace them locally. Never add
credentials, personal account identifiers, or machine-specific paths to the repo.

## Workflow

### 1. Prepare the IDE

Configure the SonarCloud or Server connection in the VS Code SonarQube extension,
then bind the workspace to the exact project. Open the repository folder, including
its source tree; a loose editor file is not a replacement for an open workspace.

C/C++ analysis requires a compilation database **and** the VS Code SonarQube
setting that selects it. Configuring Microsoft C/C++ IntelliSense or CMake Tools
alone does not establish this SonarQube setting. [VS Code requirements](https://docs.sonarsource.com/sonarqube-for-vs-code/getting-started/requirements)

Enable `CMAKE_EXPORT_COMPILE_COMMANDS` in the intended build configuration and
reconfigure it using a supported generator such as Ninja or Makefiles. For example,
from the repository root, a separate analysis build directory can be configured as:

```sh
rtk proxy cmake -S . -B build/sonar-ide -G Ninja -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Use the project's required configure options/toolchain as appropriate. An existing
build directory is preferable when it already represents the intended configuration;
keep its generator and toolchain unchanged. Confirm `compile_commands.json` exists
and contains the target translation units with usable paths and compiler arguments.

In VS Code, open **Preferences: Open Workspace Settings (JSON)** and merge these
settings with existing values. Replace the connection/project placeholders and
choose the database from the build configuration you actually use:

```json
{
  "sonarlint.connectedMode.project": {
    "connectionId": "<connection-id>",
    "projectKey": "<project-key>"
  },
  "sonarlint.pathToCompileCommands": "${workspaceFolder}/build/sonar-ide/compile_commands.json"
}
```

The connection identifier must match an existing SonarQube connection in VS Code.
Keep account-specific bindings local. Never copy a personal connection or token
into a shared guide. `sonarlint.pathToCompileCommands` is the VS Code setting;
`sonar.cfamily.compile-commands` is the analyzer property visible in the logs.
The latter belongs to analysis configuration and is not a substitute VS Code
workspace setting.

Wait for the workspace and settings changes to load. Open a source file and
manually analyze it as a baseline before introducing MCP. In **View > Output**,
select **SonarQube for IDE** and inspect the matching run. Enable verbose logs in
the extension settings if configuration details are missing. Verify the expanded
compilation database path and an actual compilation-unit count, not just a findings
count. A saved setting alone is insufficient evidence.

### 2. Configure the MCP client

Make the token available as `SONARQUBE_TOKEN` in the environment of the process
that launches the MCP server. If an existing setup exports `SONAR_TOKEN`, map it
without printing or copying its value into configuration:

```sh
export SONARQUBE_TOKEN="${SONAR_TOKEN:?Set SONAR_TOKEN in your local environment first}"
```

A shell export affects that shell and its descendants. An already running desktop
client may not inherit it; restart or launch the client with the intended environment.
Do not assume that a variable in an interactive shell reaches a desktop MCP process.

For Codex, merge this entry into `~/.codex/config.toml`, preserving other servers:

```toml
[mcp_servers.sonarqube]
command = "docker"
args = [
  "run", "--init", "--pull=always", "--rm", "-i",
  "-e", "SONARQUBE_TOKEN",
  "-e", "SONARQUBE_ORG",
  "-e", "SONARQUBE_URL",
  "-e", "SONARQUBE_IDE_PORT",
  "-e", "SONARQUBE_TOOLSETS",
  "sonarsource/sonarqube-mcp"
]
env_vars = ["SONARQUBE_TOKEN"]
enabled = true
startup_timeout_sec = 120
tool_timeout_sec = 180

[mcp_servers.sonarqube.env]
SONARQUBE_ORG = "<organization-key>"
SONARQUBE_URL = "https://sonarcloud.io"
SONARQUBE_IDE_PORT = "64121"
SONARQUBE_TOOLSETS = "ide,issues,rules,quality-gates"
```

The port above is an example, not a VS Code default. Read the port from the running
VS Code SonarQube bridge/configuration. When CLion and VS Code are both open, they
can occupy different ports; pointing MCP at the CLion port still sends analysis
to CLion. Confirm the request appears in the VS Code Output log. Restart the MCP
server after changing its port.

This is a SonarCloud EU example. For SonarCloud US, use
`https://sonarqube.us`. For SonarQube Server, remove `SONARQUBE_ORG` from both
`args` and the environment table and set `SONARQUBE_URL` to the server URL.
Use the absolute Docker executable path if the client's PATH does not contain it.

The `ide` toolset exposes `analyze_file_list`; the legacy snippet analyzer is not
needed for this C/C++ workflow. Project lookup is available without a fixed project
key. Resolve the exact key with `search_my_sonarqube_projects` before server queries.
The IDE's project binding determines the local analysis context.

The image above follows updates. Teams requiring reproducibility should pin a
verified image tag or digest and record it in their local setup documentation.
On Linux, add `"--network=host"` to the Docker arguments if the container cannot
reach the IDE's localhost bridge. Docker Desktop on macOS was verified using
`host.docker.internal` selected by the MCP image. Check bridge logs before changing
network settings. No workspace volume mount was required for the IDE bridge test;
pass paths on the IDE host, not invented container paths.

Claude Code, Cursor, and other MCP clients use the same Docker command and
environment variables, expressed in their own configuration format. The TOML
above is specific to Codex.

### 3. Verify the connection

Run these from a shell with the intended environment:

```sh
rtk proxy docker info
rtk proxy codex mcp list
```

Restart/reload the MCP client after changing its configuration. Verify that
`analyze_file_list` is available. Server initialization or project lookup proves
connectivity, not successful source analysis. Call the tool on the same file that
passed the manual IDE baseline:

```json
{
  "file_absolute_paths": [
    "/absolute/path/to/project/apps/HoroEditor/app/HoroEditorApp.cpp"
  ]
}
```

Check the tool's error flag, returned findings, and the corresponding IDE log.
Some sensors can fail while other sensors still return findings. An empty result
is not proof that a file was indexed or that every server rule ran.

### 4. Verify newly created files

A newly created source can initially be absent from the IDE's scope/index. Open
it in the correct VS Code workspace before calling `analyze_file_list`. For real
project code, add the translation unit to its owning CMake target, reconfigure the
selected build, and check for its exact path in the selected compilation database.
Do not hand-edit a generated project database as a permanent fix.

The analyzer can fall back to another compilation entry when the new file lacks
one. The log may say `using compilation database random entry`. That can borrow
C flags for a C++ file or select unrelated include paths and definitions. A returned
finding in that mode does not establish correct C++ configuration. Inspect the
chosen entry and language, then re-run using the file's own build configuration.

For a disposable smoke test, create a new `.cpp` file under the open workspace
with an intentional diagnostic, for example:

```cpp
int SonarMcpProbe()
{
    int denominator = 0;
    return 42 / denominator;
}
```

Do not build or execute this intentional error. A test-only compilation database
can describe the standalone source with the local C++ compiler and `-std=c++20`.
Select that database temporarily with `sonarlint.pathToCompileCommands`, keeping a
copy of the original workspace setting. This isolates the smoke test from engine
target changes and does not validate the engine's own build configuration.

Analyze the file, verify `cpp:S3518` and an actual C++ compilation-unit count, change
the denominator to `2`, and analyze again. The division-by-zero finding should
clear. Other rules depend on the connected Quality Profile. Restore the original
workspace settings and automatic-analysis state; remove the test source and its
temporary database. Keep these artifacts out of commits.

### 5. Analyze changed files and review findings

Use the repository's [Sonar validation contract](../../AGENTS.md#sonar-validation)
and [MCP instructions](../../.github/instructions/sonarqube_mcp.instructions.md).
For a coding task, follow those instructions for automatic-analysis toggling and
restore it at the end, including on failure.

Collect all three sets from the repository root:

```sh
rtk proxy git diff --name-only -z
rtk proxy git diff --cached --name-only -z
rtk proxy git ls-files --others --exclude-standard -z
```

An automated caller should parse the NUL delimiters, deduplicate paths, retain
existing C/C++ sources and headers, and exclude generated output and `deprecated/`
unless explicitly in scope. Convert paths to absolute IDE-host paths and submit
bounded batches through `analyze_file_list`. Include staged and untracked files;
`git diff --name-only` alone misses them. Headers may require a corresponding
translation unit with usable compiler configuration.

Report findings by severity with the rule, file, line, and explanation. Apply
safe, in-scope fixes and re-analyze the modified files. Obtain approval for an
otherwise unauthorized behavior change. Do not mark server issues resolved merely
because a local fix passes; server issue searches reflect uploaded analysis.

Reusable agent prompt:

```text
Inspect unstaged, staged, and untracked C/C++ changes under the repository rules.
Analyze existing changed files with SonarQube MCP analyze_file_list using absolute
IDE-host paths. Report findings by severity, rule, location, and reason. Apply
safe fixes within the authorized scope and re-analyze edited files. Report any
skipped files, analyzer failures, and remaining findings. Do not run SonarScanner,
upload reports, or change existing behavior without authorization.
```

## Troubleshooting

| Symptom | Check | Recovery |
| --- | --- | --- |
| Docker cannot connect | `docker info` in the client environment | Start the daemon and confirm the selected context. |
| Authentication fails | Token availability in the MCP process and Cloud region/Server URL | Use a user token and correct organization/endpoint; never log the token. |
| IDE tools are unavailable | IDE running, bridge port, Docker `-e SONARQUBE_IDE_PORT` | Correct forwarding/port and restart the MCP server. |
| Analysis request returns HTTP 500 | Matching IDE log timestamp | Read the underlying IDE exception; this alone does not imply a network failure. |
| No files belong to a configured scope, or file not found | Absolute path, open project, compiler configuration, exclusions, and index | Re-analyze a known indexed file; open the file in VS Code, regenerate the relevant compilation database, and inspect indexing. A file can exist on disk but be absent from Sonar's index. |
| Compile-commands option missing | VS Code `sonarlint.pathToCompileCommands` and the next log's `extraProperties` | Select the existing database for the intended build configuration; verify it reaches the analyzer. |
| A new C++ file borrows another compilation entry | Exact file path in the database and `random entry` log messages | Add the real source to its owning target, reconfigure, and re-analyze with its own C++ command. |
| C/C++ taint analysis disabled | Informational unsupported-feature message versus exception | Record the local limitation; do not disable C/C++ suffixes to hide it. |
| Findings returned alongside sensor errors | Full log for that exact run | Report partial validation and investigate the failing sensor separately. |

Do not repeatedly retry an unchanged failing batch. Isolate one file and compare
its IDE membership and compiler configuration with the known working baseline.

## Limitations

Local analysis applies the subset of connected-project rules supported by the
installed IDE analyzers. It is not a complete server scan. The verified analyzer
explicitly disabled C/C++ taint analysis and incremental symbolic execution in IDE
mode. `No workDir in SonarLint` warnings alone did not prevent ordinary C++ analysis.

The legacy `analyze_code_snippet` tool is not a C/C++ substitute. Do not switch to
SonarScanner or remote analysis as an implicit fallback. This workflow still needs
server access for connected configuration; “local” refers to source analysis and
the absence of an uploaded analysis report, not an offline guarantee.

## Validation Record

Verified on 2026-09-04 with macOS, Docker Desktop, SonarQube for VS Code
5.9.1, and SonarQube MCP Server 1.26.0:

- The VS Code bridge used a separate port from a concurrently running CLion instance.
- Three existing translation units completed C++ analysis with zero findings;
  this included two source files previously missing from the CLion Sonar index.
- A new temporary source initially failed scope/index lookup. After opening it in
  the workspace and selecting a test database containing its C++ command, analysis
  reported `cpp:S3518` (CRITICAL) for intentional division by zero.
- With that source still open but the original database restored, the analyzer
  borrowed an unrelated C entry and reported `c:S3518`. This demonstrated why the
  compilation entry and language must be checked even when findings are returned.
- With the explicit C++ test entry selected again, changing the denominator to `2`
  produced zero findings and one analyzed compilation unit.
- Temporary source/database files were removed, workspace settings restored
  byte-for-byte, and automatic analysis re-enabled. No report was uploaded.
- C/C++ taint analysis remained disabled with an explicit unsupported-feature log.

Earlier CLion validation on 2026-09-03 established ordinary C++ analysis but left
some existing files unindexed. This guide now uses the verified VS Code workflow.
It does not claim that every new file is immediately indexed or that every server
rule is supported locally. Linux, Windows, other clients, and SonarQube Server
were not exercised in this validation.

## References

- [SonarQube MCP Server configuration and tools](https://github.com/SonarSource/sonarqube-mcp-server)
- [Codex MCP configuration](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)
- [SonarQube for VS Code analysis](https://docs.sonarsource.com/sonarqube-for-vs-code/getting-started/running-an-analysis)
- [SonarQube Cloud connected mode](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/connected-mode)
