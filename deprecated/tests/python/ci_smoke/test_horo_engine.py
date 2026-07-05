from __future__ import annotations

from pathlib import Path

import pytest

from . import horo_engine_smoke

"""
Pytest-native smoke tests for the horo-engine CLI artifact flow.

These tests are opt-in because they require a compiled horo-engine binary and the
full round-trip cases create, build, release, and validate temporary game
projects. CI can enable them by passing --horo-engine-binary; local unit-only
pytest runs skip them cleanly.
"""


@pytest.mark.ci_smoke
@pytest.mark.parametrize(
    ("smoke_name", "function_name"),
    [
        ("help", "smoke_help"),
        ("version", "smoke_version"),
        ("no_args", "smoke_no_args"),
    ],
    ids=["help", "version", "no-args"],
)
def test_horo_engine_cli_metadata(horo_engine_binary: Path, smoke_name, function_name):
    """Validate cheap CLI metadata commands on the compiled horo-engine artifact."""
    check = getattr(horo_engine_smoke, function_name)

    assert check(str(horo_engine_binary)), smoke_name


@pytest.mark.ci_smoke
def test_horo_engine_project_dry_run(horo_engine_binary: Path):
    """Project build/release dry-run prints commands without producing binaries."""
    assert horo_engine_smoke.smoke_dry_run(str(horo_engine_binary))


@pytest.mark.ci_smoke
def test_horo_engine_unsupported_target_logs_exact_reason(horo_engine_binary: Path):
    """Unsupported local release target failures expose an exact copyable reason."""
    assert horo_engine_smoke.smoke_unsupported_target_logs(str(horo_engine_binary))


@pytest.mark.ci_smoke
def test_horo_engine_plain_release_round_trip(horo_engine_binary: Path):
    """Create, build, release, unpack, and validate a plain generated project."""
    horopak = horo_engine_smoke._find_horopak(str(horo_engine_binary))

    assert horo_engine_smoke.smoke_full_round_trip(str(horo_engine_binary), horopak)


@pytest.mark.ci_smoke
def test_horo_engine_encrypted_release_round_trip(horo_engine_binary: Path):
    """Create, build, release, unpack, and validate an encrypted generated project."""
    horopak = horo_engine_smoke._find_horopak(str(horo_engine_binary))

    assert horo_engine_smoke.smoke_encrypted_round_trip(str(horo_engine_binary), horopak)
