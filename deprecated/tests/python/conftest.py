from __future__ import annotations

from pathlib import Path

import pytest

"""
Shared pytest fixtures for Python CI infrastructure tests.

Python smoke checks live under tests/python and are executed by pytest. Tests that
need compiled C++ binaries are opt-in: they skip unless CI or a developer passes
the corresponding --*-binary / --engine-build-dir option.
"""


def pytest_addoption(parser: pytest.Parser) -> None:
    smoke = parser.getgroup("horo-engine smoke")
    smoke.addoption(
        "--horopak-binary",
        action="store",
        default=None,
        help="Path to a compiled horopak binary for pytest-native smoke tests.",
    )
    smoke.addoption(
        "--horo-engine-binary",
        action="store",
        default=None,
        help="Path to a compiled horo-engine CLI binary for pytest-native smoke tests.",
    )
    smoke.addoption(
        "--engine-root",
        action="store",
        default=None,
        help="Repository root used by submodule/SDK smoke tests. Defaults to this checkout.",
    )
    smoke.addoption(
        "--engine-build-dir",
        action="store",
        default=None,
        help="Configured and built CMake build directory for SDK/submodule smoke tests.",
    )
    smoke.addoption(
        "--build-config",
        action="store",
        default="Debug",
        help="CMake build configuration used by smoke tests. Defaults to Debug.",
    )
    smoke.addoption(
        "--submodule-fixture",
        action="store",
        default=None,
        help="Override tests/fixtures/submodule-test for SDK/submodule smoke tests.",
    )
    smoke.addoption(
        "--compiler-launcher",
        action="store",
        default=None,
        help="Compiler launcher used by fixture builds, e.g. ccache or sccache.",
    )


@pytest.fixture(scope="session")
def repo_root() -> Path:
    """Return the repository root regardless of the current pytest working directory."""
    return Path(__file__).resolve().parents[2]


@pytest.fixture(scope="session")
def horopak_binary(request: pytest.FixtureRequest) -> Path:
    """Return --horopak-binary or skip tests that require a compiled horopak."""
    binary = request.config.getoption("--horopak-binary")
    if not binary:
        pytest.skip("requires --horopak-binary")
    path = Path(binary)
    if not path.is_file():
        pytest.fail(f"horopak binary not found: {path}")
    return path


@pytest.fixture(scope="session")
def horo_engine_binary(request: pytest.FixtureRequest) -> Path:
    """Return --horo-engine-binary or skip tests that require the CLI artifact."""
    binary = request.config.getoption("--horo-engine-binary")
    if not binary:
        pytest.skip("requires --horo-engine-binary")
    path = Path(binary)
    if not path.is_file():
        pytest.fail(f"horo-engine binary not found: {path}")
    return path


@pytest.fixture(scope="session")
def smoke_engine_root(request: pytest.FixtureRequest, repo_root: Path) -> Path:
    """Repository root used by pytest-native submodule/SDK smoke tests."""
    value = request.config.getoption("--engine-root")
    path = Path(value).resolve() if value else repo_root
    if not path.is_dir():
        pytest.fail(f"engine root not found: {path}")
    return path


@pytest.fixture(scope="session")
def smoke_engine_build_dir(request: pytest.FixtureRequest) -> Path:
    """Return --engine-build-dir or skip tests that require an existing CMake build."""
    value = request.config.getoption("--engine-build-dir")
    if not value:
        pytest.skip("requires --engine-build-dir")
    path = Path(value).resolve()
    if not path.is_dir():
        pytest.fail(f"engine build dir not found: {path}")
    return path


@pytest.fixture(scope="session")
def smoke_build_config(request: pytest.FixtureRequest) -> str:
    """CMake configuration name used by smoke fixture builds."""
    return str(request.config.getoption("--build-config"))


@pytest.fixture(scope="session")
def smoke_submodule_fixture(request: pytest.FixtureRequest, smoke_engine_root: Path) -> Path:
    """Return the fixture directory used for submodule and find_package smoke tests."""
    value = request.config.getoption("--submodule-fixture")
    path = Path(value).resolve() if value else smoke_engine_root / "tests" / "fixtures" / "submodule-test"
    if not path.is_dir():
        pytest.fail(f"submodule fixture dir not found: {path}")
    return path


@pytest.fixture(scope="session")
def smoke_compiler_launcher(request: pytest.FixtureRequest) -> str | None:
    """Optional compiler launcher passed through to smoke fixture CMake invocations."""
    value = request.config.getoption("--compiler-launcher")
    return str(value) if value else None
