from __future__ import annotations

import importlib.util
from pathlib import Path


def _load_ci_module():
    script_path = Path(__file__).resolve().parents[2] / "scripts" / "ci.py"
    spec = importlib.util.spec_from_file_location("horo_ci", script_path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_clean_junit_output_dir_removes_only_xml_reports(tmp_path: Path) -> None:
    ci = _load_ci_module()
    output_dir = tmp_path / "scenarios"
    output_dir.mkdir()
    stale_report = output_dir / "test_launcher_core.xml"
    retained_file = output_dir / "notes.txt"
    stale_report.write_text("<testsuite>", encoding="utf-8")
    retained_file.write_text("keep", encoding="utf-8")

    ci.clean_junit_output_dir(str(output_dir))

    assert not stale_report.exists()
    assert retained_file.read_text(encoding="utf-8") == "keep"
