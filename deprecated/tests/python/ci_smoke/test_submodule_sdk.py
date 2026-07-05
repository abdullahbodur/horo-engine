from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

from . import submodule_sdk_smoke

"""
Pytest-native coverage for submodule and installed-SDK smoke behavior.

Fast tests isolate platform-sensitive CMake argument generation and subprocess
result accounting. The ci_smoke-marked test executes the full add_subdirectory
and find_package fixture flow when CI passes --engine-build-dir.
"""


@pytest.mark.script
@pytest.mark.parametrize(
    (
        "platform",
        "binary_ext",
        "static_lib_ext",
        "static_lib_name",
        "generator",
        "config_flag",
        "build_type",
        "binary_subdir",
    ),
    [
        (
            "win32",
            ".exe",
            ".lib",
            "HoroEngine.lib",
            ["-G", "Ninja"],
            [],
            ["-DCMAKE_BUILD_TYPE=RelWithDebInfo"],
            "",
        ),
        (
            "darwin",
            "",
            ".a",
            "libHoroEngine.a",
            ["-G", "Ninja"],
            [],
            ["-DCMAKE_BUILD_TYPE=RelWithDebInfo"],
            "",
        ),
        (
            "linux",
            "",
            ".a",
            "libHoroEngine.a",
            ["-G", "Ninja"],
            [],
            ["-DCMAKE_BUILD_TYPE=RelWithDebInfo"],
            "",
        ),
    ],
    ids=["windows-msvc", "macos-ninja", "linux-ninja"],
)
def test_platform_helpers_match_cmake_layout(
    monkeypatch,
    platform,
    binary_ext,
    static_lib_ext,
    static_lib_name,
    generator,
    config_flag,
    build_type,
    binary_subdir,
):
    """Platform helpers preserve the CMake layout expected by CI artifacts."""
    monkeypatch.setattr(submodule_sdk_smoke.sys, "platform", platform)

    assert submodule_sdk_smoke._binary_ext() == binary_ext
    assert submodule_sdk_smoke._static_lib_ext() == static_lib_ext
    assert submodule_sdk_smoke._static_lib_name("HoroEngine") == static_lib_name
    assert submodule_sdk_smoke._generator() == generator
    assert submodule_sdk_smoke._config_flag("RelWithDebInfo") == config_flag
    assert submodule_sdk_smoke._cmake_build_type("RelWithDebInfo") == build_type
    assert submodule_sdk_smoke._binary_subdir("RelWithDebInfo") == binary_subdir


@pytest.mark.script
def test_compiler_launcher_args_use_requested_launcher(monkeypatch):
    """Smoke fixture builds use compatible compiler-launcher settings."""
    monkeypatch.setattr(submodule_sdk_smoke.sys, "platform", "linux")
    assert submodule_sdk_smoke._compiler_launcher_args(None) == []
    assert submodule_sdk_smoke._compiler_launcher_args("sccache") == [
        "-DCMAKE_C_COMPILER_LAUNCHER=sccache",
        "-DCMAKE_CXX_COMPILER_LAUNCHER=sccache",
    ]

    monkeypatch.setattr(submodule_sdk_smoke.sys, "platform", "win32")
    assert submodule_sdk_smoke._compiler_launcher_args("sccache") == []


@pytest.mark.script
def test_run_check_records_success(monkeypatch):
    """Successful subprocesses increment the smoke pass counter and preserve output."""
    submodule_sdk_smoke._pass_count = 0
    submodule_sdk_smoke._fail_count = 0

    monkeypatch.setattr(
        submodule_sdk_smoke,
        "_run",
        lambda *args, **kwargs: subprocess.CompletedProcess(args[0], 0, "ok", ""),
    )

    result = submodule_sdk_smoke._run_check(["fake"], "fake command")

    assert result.returncode == 0
    assert submodule_sdk_smoke._pass_count == 1
    assert submodule_sdk_smoke._fail_count == 0


@pytest.mark.script
def test_run_check_records_failed_process(monkeypatch):
    """Failed subprocesses are reported as smoke failures without hiding stderr."""
    submodule_sdk_smoke._pass_count = 0
    submodule_sdk_smoke._fail_count = 0

    monkeypatch.setattr(
        submodule_sdk_smoke,
        "_run",
        lambda *args, **kwargs: subprocess.CompletedProcess(args[0], 7, "", "compile failed"),
    )

    result = submodule_sdk_smoke._run_check(["fake"], "fake command")

    assert result.returncode == 7
    assert submodule_sdk_smoke._pass_count == 0
    assert submodule_sdk_smoke._fail_count == 1


@pytest.mark.script
def test_failure_detail_includes_stdout_and_stderr_tail():
    """Ninja failures remain visible even when diagnostics are written to stdout."""
    result = subprocess.CompletedProcess(
        ["cmake", "--build", "build"],
        1,
        "progress\ncompiler error",
        "ninja: build stopped",
    )

    detail = submodule_sdk_smoke._failure_detail(
        result.args,
        result,
        max_lines=2,
    )

    assert "command: cmake --build build" in detail
    assert "compiler error" in detail
    assert "ninja: build stopped" in detail


@pytest.mark.script
def test_run_check_records_missing_command(monkeypatch):
    """Missing tools produce a deterministic failure result instead of raising out."""
    submodule_sdk_smoke._pass_count = 0
    submodule_sdk_smoke._fail_count = 0

    def raise_missing(*args, **kwargs):
        raise FileNotFoundError()

    monkeypatch.setattr(submodule_sdk_smoke, "_run", raise_missing)

    result = submodule_sdk_smoke._run_check(["missing-tool"], "missing command")

    assert result.returncode == -1
    assert "command not found" in result.stderr
    assert submodule_sdk_smoke._pass_count == 0
    assert submodule_sdk_smoke._fail_count == 1


@pytest.mark.ci_smoke
def test_submodule_and_installed_sdk_consumption(
    smoke_engine_root: Path,
    smoke_engine_build_dir: Path,
    smoke_submodule_fixture: Path,
    smoke_build_config: str,
    smoke_compiler_launcher: str | None,
):
    """Build and run both supported external consumption paths through pytest."""
    submodule_sdk_smoke._pass_count = 0
    submodule_sdk_smoke._fail_count = 0

    submodule_sdk_smoke._smoke_submodule(
        engine_root=smoke_engine_root,
        fixture_dir=smoke_submodule_fixture,
        build_dir=smoke_submodule_fixture / "build",
        config=smoke_build_config,
        launcher=smoke_compiler_launcher,
    )
    submodule_sdk_smoke._smoke_find_package(
        engine_root=smoke_engine_root,
        engine_build_dir=smoke_engine_build_dir,
        fixture_dir=smoke_submodule_fixture,
        config=smoke_build_config,
        launcher=smoke_compiler_launcher,
    )

    assert submodule_sdk_smoke._fail_count == 0
