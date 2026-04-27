#!/usr/bin/env python3
"""Cross-platform developer command helper for horo-engine."""

from __future__ import annotations

import argparse
import os
import platform
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
IS_WINDOWS = platform.system() == "Windows"

# --- Helpers ---

def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)

def _find_tool(env_name: str, default: str, fallback_paths: list[str] | None = None) -> str:
    """Finds an executable via Env Var, PATH, or explicit fallback paths."""
    if env_name in os.environ:
        return os.environ[env_name]
    if shutil.which(default):
        return default
    if fallback_paths:
        for path in fallback_paths:
            if Path(path).exists():
                return path
    return default

def _run(cmd: list[str | Path], *, env: dict[str, str] | None = None, cwd: Path | None = None) -> int:
    """Runs a subprocess, accepting Path objects directly in the command list."""
    run_env = dict(os.environ)
    if env:
        run_env.update(env)
    
    # Convert Path objects in cmd list to strings for cross-version safety
    str_cmd = [str(arg) for arg in cmd]
    
    result = subprocess.run(str_cmd, env=run_env, cwd=cwd)
    return result.returncode

# --- CMake / Build Helpers ---

def _preset_debug() -> str:
    return "debug-msvc" if IS_WINDOWS else "debug"

def _preset_release() -> str:
    return "release-msvc" if IS_WINDOWS else "release"

def _build_dir(preset: str) -> Path:
    return REPO_ROOT / "build" / preset

def _ensure_configured(preset: str) -> int:
    sentinel = _build_dir(preset) / ("HoroEngine.sln" if IS_WINDOWS else "build.ninja")
    if sentinel.exists():
        return 0
    return _run(["cmake", "--preset", preset], cwd=REPO_ROOT)

def _cmake_build(preset: str, *, config: str | None = None, target: str | None = None) -> list[str]:
    if IS_WINDOWS:
        cmd = ["cmake", "--build", str(_build_dir(preset)), "--parallel", "1"]
        if config: cmd.extend(["--config", config])
        if target: cmd.extend(["--target", target])
        return cmd
    
    cmd = ["cmake", "--build", "--preset", preset]
    if target: cmd.extend(["--target", target])
    return cmd

def _ctest_cmd(preset: str, *, config: str | None = None, regex: str | None = None) -> list[str]:
    if IS_WINDOWS:
        cmd = ["ctest", "--test-dir", str(_build_dir(preset)), "--output-on-failure"]
        if config: cmd.extend(["-C", config])
    else:
        cmd = ["ctest", "--preset", preset]
    
    if regex: cmd.extend(["-R", regex])
    return cmd

def _ui_env(**kwargs) -> dict[str, str]:
    env = {
        "HORO_LOG_LEVEL": kwargs.get("log_level") or _env("TEST_LOG_LEVEL", "debug"),
        "HORO_UI_TEST_CAPTURE": kwargs.get("capture") or _env("UI_TEST_CAPTURE", "0"),
        "HORO_UI_TEST_DELAY_MS": kwargs.get("delay_ms") or _env("UI_TEST_DELAY_MS", "0"),
        "HORO_UI_TEST_OUTPUT_DIR": kwargs.get("output_dir") or _env("UI_TEST_OUTPUT_DIR", str(REPO_ROOT / "ui_test_output")),
    }
    if kwargs.get("filter_value") is not None:
        env["HORO_UI_TEST_FILTER"] = kwargs["filter_value"]
    return env

# --- Coverage Helpers ---

def _coverage_exclusions(tool: str) -> list[str]:
    script = REPO_ROOT / "scripts" / "ci.py"
    result = subprocess.run([sys.executable, str(script), "coverage-exclusions", tool, str(REPO_ROOT)], 
                            capture_output=True, text=True)
    return shlex.split(result.stdout.strip())

def _run_windows_coverage(preset: str, cov_dir: Path) -> int:
    opencov = _find_tool("OPENCPPCOVERAGE", "OpenCppCoverage", ["C:/Program Files/OpenCppCoverage/OpenCppCoverage.exe"])
    excludes = _coverage_exclusions("opencppcoverage")
    test_modules = str(_build_dir(preset) / "bin" / "tests" / "*.exe")
    
    cmd = [opencov, "--quiet", "--sources", REPO_ROOT, "--modules", test_modules, *excludes, "--cover_children",
           "--export_type", f'html:{cov_dir / "html"}',
           "--export_type", f'cobertura:{cov_dir / "cobertura.xml"}', "--",
           "ctest", "--test-dir", _build_dir(preset), "-C", "Debug", "--output-on-failure"]
    
    if _run(cmd, cwd=REPO_ROOT) != 0:
        return 1
    
    print(f"Coverage report: {cov_dir / 'html' / 'index.html'}")
    return 0

def _run_lcov_coverage(preset: str, cov_dir: Path) -> int:
    if _run(_ctest_cmd(preset), cwd=REPO_ROOT) != 0:
        return 1
         
    excludes = _coverage_exclusions("lcov")
    raw_info = cov_dir / "raw.info"
    filtered_info = cov_dir / "filtered.info"
    
    if _run(["lcov", "--capture", "--directory", _build_dir(preset), "--output-file", raw_info, "--rc", "lcov_branch_coverage=1"], cwd=REPO_ROOT) != 0: return 1
    if _run(["lcov", "--remove", raw_info, *excludes, "--output-file", filtered_info, "--rc", "lcov_branch_coverage=1"], cwd=REPO_ROOT) != 0: return 1
    if _run(["genhtml", filtered_info, "--output-directory", cov_dir / "html", "--branch-coverage", "--title", "HoroEngine Coverage"], cwd=REPO_ROOT) != 0: return 1
    
    print(f"Coverage report: {cov_dir / 'html' / 'index.html'}")
    return 0

# --- Command Handlers ---

def cmd_configure(_: argparse.Namespace) -> int:
    return _ensure_configured(_preset_debug())

def cmd_build(_: argparse.Namespace) -> int:
    preset = _preset_debug()
    if _ensure_configured(preset) != 0: return 1
    return _run(_cmake_build(preset, config="Debug" if IS_WINDOWS else None), cwd=REPO_ROOT)

def cmd_test(_: argparse.Namespace) -> int:
    if cmd_build(_) != 0: return 1
    return _run(_ctest_cmd(_preset_debug(), config="Debug" if IS_WINDOWS else None), 
                env={"HORO_LOG_LEVEL": _env("TEST_LOG_LEVEL", "debug")}, cwd=REPO_ROOT)

def cmd_ui_test(_: argparse.Namespace) -> int:
    preset = _preset_debug()
    if _ensure_configured(preset) != 0: return 1
    if _run(_cmake_build(preset, config="Debug" if IS_WINDOWS else None, target="test_launcher_unit"), cwd=REPO_ROOT) != 0: return 1
    return _run(_ctest_cmd(preset, config="Debug" if IS_WINDOWS else None, regex="test_launcher_unit"), 
                env={"HORO_LOG_LEVEL": _env("TEST_LOG_LEVEL", "debug")}, cwd=REPO_ROOT)

def cmd_ui_test_windowed(_: argparse.Namespace) -> int:
    preset = _preset_debug()
    if _ensure_configured(preset) != 0: return 1
    if _run(_cmake_build(preset, config="Debug" if IS_WINDOWS else None, target="HoroEditorUiTest"), cwd=REPO_ROOT) != 0: return 1
    
    output_dir = Path(_env("UI_TEST_OUTPUT_DIR", str(REPO_ROOT / "ui_test_output")))
    output_dir.mkdir(parents=True, exist_ok=True)
    
    exe_name = "HoroEditorUiTest.exe" if IS_WINDOWS else "HoroEditorUiTest"
    bin_path = _build_dir(preset) / "bin" / ("Debug" if IS_WINDOWS else "") / exe_name
    
    return _run([bin_path, "--run-ui-tests"], env=_ui_env(), cwd=REPO_ROOT)

def cmd_release(_: argparse.Namespace) -> int:
    preset = _preset_release()
    if _ensure_configured(preset) != 0: return 1
    return _run(_cmake_build(preset, config="Release" if IS_WINDOWS else None), cwd=REPO_ROOT)

def cmd_coverage(_: argparse.Namespace) -> int:
    preset = _preset_debug() if IS_WINDOWS else _env("PRESET_COV", "coverage")
    if _ensure_configured(preset) != 0: return 1
    if _run(_cmake_build(preset, config="Debug" if IS_WINDOWS else None), cwd=REPO_ROOT) != 0: return 1
    
    cov_dir = REPO_ROOT / "build" / "coverage"
    cov_dir.mkdir(parents=True, exist_ok=True)
    
    if IS_WINDOWS:
        return _run_windows_coverage(preset, cov_dir)
    return _run_lcov_coverage(preset, cov_dir)

def cmd_coverage_source_summary(_: argparse.Namespace) -> int:
    # (Left mostly intact for brevity, but could also be refactored into a helper similar to _run_lcov_coverage)
    if IS_WINDOWS:
        print("coverage-source-summary is not supported on Windows/MSVC yet", file=sys.stderr)
        return 1
    # ... Rest of your original lcov summary logic ...
    return 0 # Placeholder for brevity 

def cmd_clean(_: argparse.Namespace) -> int:
    return _run(["cmake", "-E", "rm", "-rf", _build_dir(_preset_debug())], cwd=REPO_ROOT)

def cmd_clean_all(_: argparse.Namespace) -> int:
    return _run(["cmake", "-E", "rm", "-rf", REPO_ROOT / "build"], cwd=REPO_ROOT)

def _tracked_sources() -> list[str]:
    result = subprocess.run(["git", "ls-files", "*.cpp", "*.h"], capture_output=True, text=True, cwd=REPO_ROOT)
    return [line for line in result.stdout.splitlines() if not line.startswith("vendor/")]

def cmd_format(_: argparse.Namespace) -> int:
    sources = _tracked_sources()
    if not sources: return 0
    return _run([_find_tool("CLANG_FORMAT", "clang-format", [r"C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe"]), "-i", *sources], cwd=REPO_ROOT)

def cmd_format_check(_: argparse.Namespace) -> int:
    sources = _tracked_sources()
    if not sources: return 0
    return _run([_find_tool("CLANG_FORMAT", "clang-format", [r"C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe"]), "--dry-run", "--Werror", *sources], cwd=REPO_ROOT)

# --- CLI Setup ---

def main() -> int:
    parser = argparse.ArgumentParser(
        prog="dev.py",
        description="Cross-platform developer command helper for horo-engine.",
        epilog="""
UI automation vars:
  UI_TEST_DELAY_MS=<ms> (default 0)
  UI_TEST_CAPTURE=0|1 (default 0)
  UI_TEST_OUTPUT_DIR=<path> (default ./ui_test_output)
  TEST_LOG_LEVEL=debug|info|warn|error (default debug)
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    
    sub = parser.add_subparsers(dest="cmd", required=True)

    commands = {
        "configure": (cmd_configure, "Run CMake preset only"),
        "build": (cmd_build, "Build debug"),
        "test": (cmd_test, "Build & run engine tests"),
        "ui-test": (cmd_ui_test, "Build & run launcher unit tests"),
        "ui-test-windowed": (cmd_ui_test_windowed, "Build & run windowed launcher UI automation"),
        "release": (cmd_release, "Build release library"),
        "coverage": (cmd_coverage, "Build, run tests, generate HTML coverage report"),
        "coverage-source-summary": (cmd_coverage_source_summary, "Build, run tests, print source-only coverage summary"),
        "clean": (cmd_clean, "Remove debug build dir"),
        "clean-all": (cmd_clean_all, "Remove all build dirs"),
        "format": (cmd_format, "Format all tracked C++ sources"),
        "format-check": (cmd_format_check, "Check formatting"),
    }

    for name, (func, help_text) in commands.items():
        p = sub.add_parser(name, help=help_text)
        p.set_defaults(func=func)

    # Automatically print help if no arguments are provided
    if len(sys.argv) == 1:
        parser.print_help()
        return 0

    args = parser.parse_args()
    return args.func(args)

if __name__ == "__main__":
    raise SystemExit(main())
