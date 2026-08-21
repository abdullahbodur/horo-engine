# Horo Grafana dashboards

The versioned [HoroEditor observability dashboard](./dashboards/horo-editor.json)
visualizes the metrics and structured logs exported by the optional local
OpenTelemetry adapter.

## Import

The stable public download URL is:

```text
https://raw.githubusercontent.com/abdullahbodur/horo-engine/main/observability/grafana/dashboards/horo-editor.json
```

Download it directly when the repository checkout is not available:

```bash
curl -fL \
  -o horo-editor-grafana-dashboard.json \
  https://raw.githubusercontent.com/abdullahbodur/horo-engine/main/observability/grafana/dashboards/horo-editor.json
```

In Grafana, open **Dashboards → New → Import** and upload the downloaded JSON
file, paste its JSON contents, or consume the raw URL from dashboard-as-code
automation that supports Git-hosted resources.

Select a Prometheus datasource for the `Prometheus` variable and a Loki
datasource for the `Loki` variable. Grafana LGTM names these datasources
`Prometheus` and `Loki` by default. The dashboard does not embed a datasource
UID, collector address, token, or other machine-specific setting.

Start the editor with local export enabled:

```bash
python3 scripts/dev.py run editor
```

The developer runner checks local Grafana after a successful editor build and
automatically imports this dashboard when it is missing or its repository JSON
has changed. A source-hash tag prevents unchanged editor starts from creating
new Grafana dashboard versions. Configure or disable this behavior in
`.env.local`:

```dotenv
HORO_DEV_GRAFANA_AUTO_IMPORT=ON
HORO_GRAFANA_URL=http://127.0.0.1:3000
```

Automatic import is intentionally restricted to loopback Grafana URLs. An
unavailable or authenticated Grafana instance never prevents editor startup.

The dashboard expects the stable HoroEditor service identity `horo-editor`, the
four host metrics documented in the observability contract, and structured Loki
metadata including `severity_text` and `log_category`.
