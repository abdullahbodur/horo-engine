# Local C/C++ Analysis with SonarQube MCP

## Purpose

Use an AI coding agent to analyze changed C/C++ files through SonarQube MCP and
an open SonarQube for IDE instance. Connected mode applies the project's supported
Quality Profile rules locally. This workflow does not run SonarScanner or upload
an analysis report; it does not replace CI analysis or a server Quality Gate.

```text
AI coding agent -> SonarQube MCP -> SonarQube for IDE -> local source files
                                         |
                                         +-> connected project rules/settings
```

## Prerequisites

- A running Docker daemon and an MCP-capable coding agent.
- CLion with SonarQube for IDE installed and the repository open.
- A loaded CMake profile with supported compiler information for the target files.
- A SonarQube Cloud (SonarCloud) or Server connection, a user token with access to
  the project, and IDE connected-mode binding to that exact project.
- The IDE bridge port, normally in the 64120–64130 range. Use the port reported by
  the IDE; do not assume every running IDE uses 64120.

Examples use `<organization-key>`, `<project-key>`, and
`/absolute/path/to/project` as placeholders. Replace them locally. Never add
credentials, personal account identifiers, or machine-specific paths to the repo.

## Workflow

### 1. Prepare the IDE

Configure the connection and project binding in **SonarQube for IDE** settings.
Keep the IDE open, let CMake loading/indexing complete, and manually analyze one
file that belongs to the selected build configuration. This establishes an IDE
baseline before introducing MCP.

Enable compilation database export in the intended CMake profile with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then reload CMake. With a supported generator
such as Ninja or Makefiles, this produces `compile_commands.json` in that profile's
build directory. Confirm it contains the target translation units and local paths.

If the analyzer reports that `sonar.cfamily.compile-commands` is missing, add it
under **SonarQube for IDE > Analysis Properties**:

```properties
sonar.cfamily.compile-commands=/absolute/path/to/project/<active-build-directory>/compile_commands.json
```

Use the actual build directory and keep it aligned with the active IDE profile.
Check the next analysis log for the expanded path in `extraProperties` and
`CFamily analysis configuration mode: Compile-Commands`. A saved setting alone
is insufficient evidence. This setting does not repair missing IDE index entries.

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
SONARQUBE_IDE_PORT = "64120"
SONARQUBE_TOOLSETS = "ide,issues,rules,quality-gates"
```

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

### 4. Analyze changed files and review findings

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
| No files belong to a configured scope, or file not found | Absolute path, open project, compiler configuration, exclusions, and index | Re-analyze a known indexed file; reload the relevant CMake profile and inspect IDE indexing. A file can exist on disk but be absent from Sonar's index. |
| Compile-commands option missing | Analysis Properties and the next log's `extraProperties` | Supply the existing database from the intended build profile; verify it reaches the analyzer. |
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

Verified on 2026-09-03 with macOS, Docker Desktop, CLion 2026.1, SonarQube for IDE
12.8.0, and SonarQube MCP Server 1.26.0:

- MCP initialization and connected-project lookup succeeded.
- `analyze_file_list` returned two findings for an indexed editor C++ file.
- After adding the compilation database property, IDE logs confirmed
  Compile-Commands mode and completed ordinary C++ analysis.
- C/C++ taint analysis was skipped with an explicit unsupported-feature message.
- Other existing source files remained absent from the IDE index. Complete
  repository coverage was not established; compilation database configuration
  alone did not resolve this separate failure.
- No source changes or report uploads were required for connection verification.

Linux, Windows, alternate clients, and SonarQube Server were not exercised in this
validation. Their setup variations should be checked against current vendor docs.

## References

- [SonarQube MCP Server configuration and tools](https://github.com/SonarSource/sonarqube-mcp-server)
- [Codex MCP configuration](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)
- [SonarQube for IntelliJ analysis](https://docs.sonarsource.com/sonarqube-for-intellij/using/scan-my-project)
- [SonarQube Cloud connected mode](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/connected-mode)
