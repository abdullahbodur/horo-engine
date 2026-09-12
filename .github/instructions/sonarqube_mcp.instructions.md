---
applyTo: "**/*"
---

# SonarQube local-analysis policy

The SonarQube CLI (`sonar`) is the single supported local-analysis workflow for
this repository. The former VS Code SonarQube for IDE bridge and SonarQube MCP
IDE tools are not local-analysis fallbacks.

## Required workflow

- Run from the worktree being inspected:
  `sonar analyze --project <project-key> --format json --depth STANDARD`.
- With no selector, the CLI analyzes staged, unstaged, and untracked changes.
  Use `--staged`, `--base <ref>`, or repeated `--file <path>` only for an
  intentional narrower scope.
- Resolve the exact project key with `sonar list projects --query <name>`;
  never invent a key.
- Prefer `SONARQUBE_CLI_TOKEN`, `SONARQUBE_CLI_ORG`, and
  `SONARQUBE_CLI_SERVER` environment variables for ephemeral runs. Do not put
  credentials in the repository or command output.
- Report `secrets` and `agentic` results separately. A clean secrets result is
  not a clean quality result when `agentic` contains skipped files, failures, or
  `globalError`.
- For C/C++, verify that the intended files appear in `agentic.files` and that a
  long-lived branch has a successful CI analysis supplying Vortex build context.

## Entitlement and failure handling

`403 Forbidden` or `Vortex analysis is not available on this connection` means
the account or project lacks the required Agentic/Vortex entitlement. Report it
as a failed quality validation; do not silently fall back to VS Code, MCP, or
`sonar-scanner`.

Do not repeatedly retry an unchanged authorization or entitlement failure. Local
secrets scanning may still be reported, but it must remain clearly separate from
Agentic/Vortex quality findings.

## Prohibited substitutions

- Do not call `analyze_file_list`, `toggle_automatic_analysis`, or
  `analyze_code_snippet` for repository validation.
- Do not run `sonar-scanner` for local uncommitted-change feedback; it is the
  full-project CI scanner.
- Do not claim success from an empty issue list unless the intended files were
  analyzed and no skips, failures, or global errors were returned.
