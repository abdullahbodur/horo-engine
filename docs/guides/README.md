# Horo Engine Guides

This directory contains implementation-facing guides for developers who build on
Horo Engine and contributors working on its development tooling. Architecture
documents define the contracts; guides show how to use those contracts in concrete
workflows.

## Available Guides

- [Extension Module Development](./extension-module-development.md): build an
  add-on package that contributes editor tabs, Settings pages, MCP tools,
  commands, and data-bus observers through the extension ABI/API.
- [Local C/C++ Analysis with SonarQube MCP and VS Code](./sonarqube-mcp-local-analysis.md):
  configure the IDE bridge, analyze local changes, and diagnose partial results.

## Writing a Guide

Start new guides from [Guide Template](./guide-template.md). Use its Purpose,
Prerequisites, Workflow, Troubleshooting, Limitations, Validation Record, and
References sections; adapt the workflow steps to the task. Use lowercase hyphenated
filenames and add the finished guide to this index.

Use generic placeholders for accounts, organization/project keys, and absolute
paths. Keep secrets out of examples. Include expected results and actual verification
limits. Link architecture contracts instead of redefining them. Existing guides can
adopt the template when substantively revised; avoid format-only rewrites.
