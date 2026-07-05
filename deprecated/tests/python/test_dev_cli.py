from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


def _load_dev_module():
    script_path = Path(__file__).resolve().parents[2] / "scripts" / "dev.py"
    spec = importlib.util.spec_from_file_location("horo_dev", script_path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_run_launcher_is_a_hierarchical_command() -> None:
    dev = _load_dev_module()

    args = dev.build_parser().parse_args(
        [
            "run",
            "launcher",
            "--no-build",
            "--jobs",
            "8",
            "--",
            "--project",
            "sample",
        ]
    )

    assert args.run_command == "launcher"
    assert args.no_build is True
    assert args.jobs == 8
    assert args.launcher_args == ["--", "--project", "sample"]
    assert args.func is dev.cmd_run_launcher


def test_project_release_is_a_hierarchical_command() -> None:
    dev = _load_dev_module()

    args = dev.build_parser().parse_args(
        ["project", "release", "/tmp/sample", "--version", "1.2.3"]
    )

    assert args.project_command == "release"
    assert args.path == "/tmp/sample"
    assert args.version == "1.2.3"
    assert args.func is dev.cmd_project_release


def test_flat_legacy_command_is_rejected() -> None:
    dev = _load_dev_module()

    with pytest.raises(SystemExit):
        dev.build_parser().parse_args(["run-launcher"])


def test_windows_engine_binary_uses_ninja_output_layout(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    dev = _load_dev_module()
    monkeypatch.setattr(dev, "IS_WINDOWS", True)

    launcher = dev._launcher_binary("debug-msvc")
    cli = dev._cli_binary("debug-msvc")

    assert launcher == dev.REPO_ROOT / "build/debug-msvc/bin/HoroEditor.exe"
    assert cli == dev.REPO_ROOT / "build/debug-msvc/bin/horo-engine.exe"
