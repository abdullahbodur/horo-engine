from __future__ import annotations

import json
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DASHBOARD_PATH = REPOSITORY_ROOT / "observability" / "grafana" / "dashboards" / "horo-editor.json"
PROMETHEUS_METRICS = {
    "horo_editor_frame_duration_seconds",
    "horo_editor_frame_number_frames",
    "horo_observability_records_dropped",
    "horo_observability_sink_failures",
}


def load_dashboard() -> dict[str, object]:
    return json.loads(DASHBOARD_PATH.read_text(encoding="utf-8"))


def test_dashboard_has_stable_identity_and_portable_datasources() -> None:
    dashboard = load_dashboard()
    assert dashboard["uid"] == "horo-editor-observability"
    assert dashboard["title"] == "HoroEditor Observability"
    assert dashboard["schemaVersion"] >= 39

    variables = {variable["name"]: variable for variable in dashboard["templating"]["list"]}
    assert variables["prometheus"]["type"] == "datasource"
    assert variables["prometheus"]["query"] == "prometheus"
    assert variables["loki"]["type"] == "datasource"
    assert variables["loki"]["query"] == "loki"

    datasource_uids = {panel["datasource"]["uid"] for panel in dashboard["panels"]}
    assert datasource_uids == {"${prometheus}", "${loki}"}


def test_dashboard_queries_cover_exported_metrics_and_structured_logs() -> None:
    dashboard = load_dashboard()
    expressions = [target["expr"] for panel in dashboard["panels"] for target in panel.get("targets", [])]
    expression_text = "\n".join(expressions)

    assert PROMETHEUS_METRICS <= {metric for metric in PROMETHEUS_METRICS if metric in expression_text}
    assert '{service_name="horo-editor"}' in expression_text
    assert "severity_text" in expression_text
    assert "log_category" in expression_text


def test_dashboard_panel_grid_is_bounded_and_non_overlapping() -> None:
    panels = load_dashboard()["panels"]
    occupied: set[tuple[int, int]] = set()

    for panel in panels:
        grid = panel["gridPos"]
        assert 0 <= grid["x"] < 24
        assert 1 <= grid["w"] <= 24
        assert grid["x"] + grid["w"] <= 24
        assert grid["h"] > 0

        cells = {
            (x, y)
            for x in range(grid["x"], grid["x"] + grid["w"])
            for y in range(grid["y"], grid["y"] + grid["h"])
        }
        assert occupied.isdisjoint(cells), f"panel {panel['title']} overlaps another panel"
        occupied.update(cells)
