# Scripts

New repository scripts belong here. Legacy scripts are under `deprecated/scripts/` until migrated.

## Local editor runner

Copy `.env.example` to `.env.local`, adjust the local OpenTelemetry endpoint if
needed, then use the narrow developer runner:

```bash
python3 scripts/dev.py run editor
python3 scripts/dev.py run editor -- --project /path/to/project
```

`HORO_DEV_OTEL_EXPORT` is interpreted only by `dev.py`. When enabled, the runner
configures the optional adapter, performs a TCP reachability check, and launches
the editor with explicit export approval. It does not start or manage Docker,
Grafana, or an OpenTelemetry Collector.

When local Grafana is reachable, the runner also synchronizes the repository
HoroEditor dashboard before launch. This import can be disabled with
`HORO_DEV_GRAFANA_AUTO_IMPORT=OFF`; Grafana availability never controls whether
the editor is allowed to start.
