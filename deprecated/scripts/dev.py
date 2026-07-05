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
import tempfile
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

    str_cmd = [str(arg) for arg in cmd]
    print("+", shlex.join(str_cmd), flush=True)
    try:
        result = subprocess.run(str_cmd, env=run_env, cwd=cwd)
        return result.returncode
    except FileNotFoundError:
        print(f"Required command not found: {str_cmd[0]}", file=sys.stderr)
        return 127

# --- CMake / Build Helpers ---

def _preset_debug() -> str:
    return "debug-msvc" if IS_WINDOWS else "debug"

def _preset_release() -> str:
    return "release-msvc" if IS_WINDOWS else "release"


def _preset_for_config(config: str) -> str:
    return _preset_release() if config == "release" else _preset_debug()


def _selected_preset(args: argparse.Namespace, default_config: str = "debug") -> str:
    preset = getattr(args, "preset", None)
    if preset:
        return preset
    return _preset_for_config(getattr(args, "config", default_config))


def _build_config_for_preset(preset: str) -> str | None:
    if not IS_WINDOWS:
        return None
    return "Release" if "release" in preset.lower() else "Debug"


def _build_dir(preset: str) -> Path:
    return REPO_ROOT / "build" / preset


def _configure(preset: str, *, fresh: bool = False) -> int:
    cmd = ["cmake"]
    if fresh:
        cmd.append("--fresh")
    cmd.extend(["--preset", preset])
    return _run(cmd, cwd=REPO_ROOT)


def _ensure_configured(preset: str) -> int:
    sentinel = _build_dir(preset) / "build.ninja"
    if sentinel.exists():
        return 0
    return _configure(preset)


def _cmake_build(
    preset: str,
    *,
    config: str | None = None,
    target: str | None = None,
    jobs: int | None = None,
) -> list[str]:
    if IS_WINDOWS:
        cmd = ["cmake", "--build", str(_build_dir(preset))]
        cmd.extend(["--parallel", str(jobs or 1)])
        if config: cmd.extend(["--config", config])
        if target: cmd.extend(["--target", target])
        return cmd

    cmd = ["cmake", "--build", "--preset", preset]
    if jobs is not None:
        cmd.extend(["--parallel", str(jobs)])
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
    filtered_info = _collect_lcov_info(preset, cov_dir)
    if filtered_info is None:
        return 1

    if _run(
        [
            "genhtml",
            filtered_info,
            "--output-directory",
            cov_dir / "html",
            "--branch-coverage",
            "--title",
            "HoroEngine Coverage",
        ],
        cwd=REPO_ROOT,
    ) != 0:
        return 1

    print(f"Coverage report: {cov_dir / 'html' / 'index.html'}")
    return 0


def _collect_lcov_info(preset: str, cov_dir: Path) -> Path | None:
    if _run(_ctest_cmd(preset), cwd=REPO_ROOT) != 0:
        return None

    excludes = _coverage_exclusions("lcov")
    raw_info = cov_dir / "raw.info"
    filtered_info = cov_dir / "filtered.info"

    if _run(
        ["lcov", "--capture", "--directory", _build_dir(preset), "--output-file", raw_info, "--rc", "lcov_branch_coverage=1"],
        cwd=REPO_ROOT,
    ) != 0:
        return None
    if _run(
        ["lcov", "--remove", raw_info, *excludes, "--output-file", filtered_info, "--rc", "lcov_branch_coverage=1"],
        cwd=REPO_ROOT,
    ) != 0:
        return None
    return filtered_info

# --- Command Handlers ---

def cmd_configure(args: argparse.Namespace) -> int:
    return _configure(_selected_preset(args), fresh=args.fresh)


def cmd_build(args: argparse.Namespace) -> int:
    preset = _selected_preset(args)
    if _ensure_configured(preset) != 0:
        return 1
    build_config = _build_config_for_preset(preset)
    return _run(
        _cmake_build(
            preset,
            config=build_config,
            target=args.target,
            jobs=args.jobs,
        ),
        cwd=REPO_ROOT,
    )


def cmd_test(args: argparse.Namespace) -> int:
    preset = _selected_preset(args)
    build_config = "Debug" if IS_WINDOWS else None
    if not args.no_build:
        build_args = argparse.Namespace(
            preset=preset,
            config="debug",
            target=None,
            jobs=args.jobs,
        )
        if cmd_build(build_args) != 0:
            return 1
    return _run(
        _ctest_cmd(preset, config=build_config, regex=args.filter),
        env={"HORO_LOG_LEVEL": args.log_level},
        cwd=REPO_ROOT,
    )


def cmd_ui_test(args: argparse.Namespace) -> int:
    preset = _selected_preset(args)
    build_config = "Debug" if IS_WINDOWS else None
    target = "HoroEditorUiTest" if args.windowed else "test_launcher_unit"
    if _ensure_configured(preset) != 0:
        return 1
    if not args.no_build and _run(
        _cmake_build(
            preset,
            config=build_config,
            target=target,
            jobs=args.jobs,
        ),
        cwd=REPO_ROOT,
    ) != 0:
        return 1

    if not args.windowed:
        return _run(
            _ctest_cmd(preset, config=build_config, regex="test_launcher_unit"),
            env={"HORO_LOG_LEVEL": args.log_level},
            cwd=REPO_ROOT,
        )

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    exe_name = "HoroEditorUiTest.exe" if IS_WINDOWS else "HoroEditorUiTest"
    bin_path = _build_dir(preset) / "bin" / exe_name

    return _run(
        [bin_path, "--run-ui-tests"],
        env=_ui_env(
            log_level=args.log_level,
            capture=args.capture,
            delay_ms=args.delay_ms,
            output_dir=args.output_dir,
            filter_value=args.filter,
        ),
        cwd=REPO_ROOT,
    )

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
    if IS_WINDOWS:
        print("coverage-source-summary is not supported on Windows/MSVC yet", file=sys.stderr)
        return 1

    preset = _env("PRESET_COV", "coverage")
    if _ensure_configured(preset) != 0:
        return 1
    if _run(_cmake_build(preset), cwd=REPO_ROOT) != 0:
        return 1

    cov_dir = REPO_ROOT / "build" / "coverage"
    cov_dir.mkdir(parents=True, exist_ok=True)

    filtered_info = _collect_lcov_info(preset, cov_dir)
    if filtered_info is None:
        return 1

    return _run(
        ["lcov", "--summary", filtered_info, "--rc", "lcov_branch_coverage=1"],
        cwd=REPO_ROOT,
    )

def _starter_binary(config_name: str) -> Path:
    """Resolves the starter app binary when engine is used as a submodule.

    Assumes this script lives at <starter_root>/engine/scripts/dev.py,
    so REPO_ROOT.parent == starter_root.

    On Windows (MSVC multi-config), CMake places the binary under bin/Debug/
    or bin/Release/ rather than directly under bin/.
    """
    suffix = ".exe" if IS_WINDOWS else ""
    msvc_subdir = "Debug" if IS_WINDOWS else ""
    return REPO_ROOT.parent / "build" / config_name / "bin" / msvc_subdir / f"HoroStarterApp{suffix}"

def _launcher_binary(preset: str) -> Path:
    suffix = ".exe" if IS_WINDOWS else ""
    return _build_dir(preset) / "bin" / f"HoroEditor{suffix}"

def _cli_binary(preset: str | None = None) -> Path:
    preset = preset or _preset_debug()
    suffix = ".exe" if IS_WINDOWS else ""
    return _build_dir(preset) / "bin" / f"horo-engine{suffix}"

def _sdk_root(preset: str | None = None) -> Path:
    return _build_dir(preset or _preset_debug())

def _default_project_root() -> Path:
    return Path(_env("HORO_PROJECT", str(Path(tempfile.gettempdir()) / "horo_cli_sample")))

def _project_name_from_root(project_root: Path) -> str:
    return _env("HORO_PROJECT_NAME", project_root.name or "HoroCliSample")

def _build_cli() -> int:
    preset = _preset_debug()
    build_config = "Debug" if IS_WINDOWS else None
    if _ensure_configured(preset) != 0:
        return 1
    return _run(_cmake_build(preset, config=build_config, target="horo-engine"), cwd=REPO_ROOT)

def _run_cli(args: list[str | Path], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> int:
    if _build_cli() != 0:
        return 1
    return _run([_cli_binary(), *args], cwd=cwd or REPO_ROOT, env=env)

def cmd_run_launcher(args: argparse.Namespace) -> int:
    preset = _selected_preset(args)
    build_config = _build_config_for_preset(preset)
    if _ensure_configured(preset) != 0:
        return 1
    if not args.no_build and _run(
        _cmake_build(
            preset,
            config=build_config,
            target="HoroEditor",
            jobs=args.jobs,
        ),
        cwd=REPO_ROOT,
    ) != 0:
        return 1
    launcher_args = args.launcher_args
    if launcher_args[:1] == ["--"]:
        launcher_args = launcher_args[1:]
    return _run([_launcher_binary(preset), *launcher_args])

def cmd_cli_help(_: argparse.Namespace) -> int:
    return _run_cli(["--help"])

def cmd_project_create(args: argparse.Namespace) -> int:
    project_root = Path(args.path) if args.path else _default_project_root()
    return _run_cli([
        "project", "create", project_root,
        "--name", args.name or _project_name_from_root(project_root),
        "--sdk-root", _sdk_root(),
    ])

def cmd_project_build(args: argparse.Namespace) -> int:
    project_root = Path(args.path) if args.path else _default_project_root()
    return _run_cli([
        "project", "build", project_root,
        "--config", args.config,
        "--sdk-root", _sdk_root(),
    ] + (["--dry-run"] if args.dry_run else []))

def cmd_project_release(args: argparse.Namespace) -> int:
    project_root = Path(args.path) if args.path else _default_project_root()
    command = [
        "project", "release", project_root,
        "--version", args.version,
        "--config", args.config,
        "--sdk-root", _sdk_root(),
    ]
    if args.output_root:
        command.extend(["--output-root", args.output_root])
    if args.archive_password:
        command.extend(["--archive-password", args.archive_password])
    if args.dry_run:
        command.append("--dry-run")
    return _run_cli(command)

def cmd_project_open(args: argparse.Namespace) -> int:
    project_root = Path(args.path) if args.path else _default_project_root()
    command = [
        "project", "open", project_root,
        "--sdk-root", _sdk_root(),
    ]
    if args.dry_run:
        command.append("--dry-run")
    return _run_cli(command)

def cmd_project_release_smoke(args: argparse.Namespace) -> int:
    project_root = Path(args.path) if args.path else Path(tempfile.gettempdir()) / "horo_release_smoke_project"
    password = args.archive_password or _env("HORO_RELEASE_ARCHIVE_PASSWORD", "dev-smoke-password")
    if args.clean and project_root.exists():
        shutil.rmtree(project_root)

    create_code = cmd_project_create(argparse.Namespace(
        path=str(project_root),
        name=args.name or _project_name_from_root(project_root),
    ))
    if create_code != 0:
        return create_code

    release_code = cmd_project_release(argparse.Namespace(
        path=str(project_root),
        version=args.version,
        config=args.config,
        output_root=None,
        archive_password=password,
        dry_run=False,
    ))
    if release_code != 0:
        return release_code

    release_dir = project_root / "build" / "release"
    candidates = sorted(release_dir.glob(f"v{args.version}_*"))
    if not candidates:
        print(f"No release output found under {release_dir}", file=sys.stderr)
        return 1
    output_path = candidates[-1]
    json_files = list(output_path.rglob("*.json"))
    if json_files:
        print("Raw JSON files remain in release output:", file=sys.stderr)
        for path in json_files:
            print(f"  {path}", file=sys.stderr)
        return 1
    if not (output_path / "assets.horo").is_file():
        print(f"Missing release archive: {output_path / 'assets.horo'}", file=sys.stderr)
        return 1

    binary_name = args.name or _project_name_from_root(project_root)
    binary = output_path / (binary_name + (".exe" if IS_WINDOWS else ""))
    if not binary.exists():
        binaries = [p for p in output_path.iterdir() if p.is_file() and os.access(p, os.X_OK)]
        binary = binaries[0] if binaries else binary
    if not binary.exists():
        print(f"Release binary not found in {output_path}", file=sys.stderr)
        return 1

    print(f"Release output: {output_path}")
    print("Running packaged binary for smoke validation...")
    process = subprocess.Popen(
        [str(binary)],
        cwd=output_path,
        env={**os.environ, "HORO_RELEASE_ARCHIVE_PASSWORD": password},
    )
    try:
        return_code = process.wait(timeout=float(args.run_seconds))
        print(f"Release binary exited with code {return_code}", file=sys.stderr)
        return return_code
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()
        print("Release binary stayed alive during smoke window.")
        return 0

def cmd_mcp_release_example(args: argparse.Namespace) -> int:
    project_root = Path(args.path) if args.path else _default_project_root()
    payload = {
        "tool": "project.release",
        "arguments": {
            "projectRoot": str(project_root),
            "version": args.version,
            "config": args.config,
            "archivePassword": args.archive_password or "${HORO_RELEASE_ARCHIVE_PASSWORD}",
        },
    }
    import json
    print(json.dumps(payload, indent=2))
    return 0

def cmd_run_editor(args: argparse.Namespace) -> int:
    config = args.config if args.config else _preset_debug()
    binary = _starter_binary(config)
    if not binary.exists():
        print(f"Binary not found: {binary}", file=sys.stderr)
        print(
            f"Build first: {sys.executable} scripts/dev.py build --preset {config}",
            file=sys.stderr,
        )
        return 1
    return _run([binary, "--editor"])

def cmd_run_play(args: argparse.Namespace) -> int:
    config = args.config if args.config else _preset_debug()
    binary = _starter_binary(config)
    if not binary.exists():
        print(f"Binary not found: {binary}", file=sys.stderr)
        print(
            f"Build first: {sys.executable} scripts/dev.py build --preset {config}",
            file=sys.stderr,
        )
        return 1
    return _run([binary, "--play"])


def cmd_clean(args: argparse.Namespace) -> int:
    target = REPO_ROOT / "build" if args.all else _build_dir(_selected_preset(args))
    return _run(["cmake", "-E", "rm", "-rf", target], cwd=REPO_ROOT)

def _tracked_sources() -> list[str]:
    result = subprocess.run(["git", "ls-files", "*.cpp", "*.h"], capture_output=True, text=True, cwd=REPO_ROOT)
    return [line for line in result.stdout.splitlines() if not line.startswith("vendor/")]

def cmd_format(args: argparse.Namespace) -> int:
    sources = _tracked_sources()
    if not sources: return 0
    clang_format = _find_tool(
        "CLANG_FORMAT",
        "clang-format",
        [r"C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe"],
    )
    format_args = ["--dry-run", "--Werror"] if args.check else ["-i"]
    return _run([clang_format, *format_args, *sources], cwd=REPO_ROOT)

# --- CLI Setup ---

def _add_engine_config_arguments(
    parser: argparse.ArgumentParser,
    *,
    allow_release: bool = True,
) -> None:
    group = parser.add_mutually_exclusive_group()
    configs = ["debug", "release"] if allow_release else ["debug"]
    group.add_argument(
        "--config",
        choices=configs,
        default="debug",
        help="Logical build configuration (default: debug)",
    )
    group.add_argument(
        "--preset",
        help="Explicit CMake preset override",
    )


def _add_build_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--target", help="Build only the named CMake target")
    parser.add_argument(
        "--jobs",
        type=int,
        metavar="N",
        help="Override the parallel build job count",
    )


def _add_project_path(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "path",
        nargs="?",
        help="Project root (default: HORO_PROJECT or a temporary sample directory)",
    )


def _add_project_config(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--config",
        default="Release",
        choices=["Debug", "Release", "MinSizeRel"],
        help="Project build configuration (default: Release)",
    )


def _add_release_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--version",
        default=_env("HORO_VERSION", "0.0.1"),
        help="Release version, with or without a leading v",
    )
    parser.add_argument(
        "--archive-password",
        help="Release archive password; may also come from HORO_RELEASE_ARCHIVE_PASSWORD",
    )


def build_parser() -> argparse.ArgumentParser:
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

    configure = sub.add_parser("configure", help="Configure a CMake preset")
    _add_engine_config_arguments(configure)
    configure.add_argument(
        "--fresh",
        action="store_true",
        help="Discard the existing CMake cache before configuring",
    )
    configure.set_defaults(func=cmd_configure)

    build = sub.add_parser("build", help="Configure if needed and build the engine")
    _add_engine_config_arguments(build)
    _add_build_arguments(build)
    build.set_defaults(func=cmd_build)

    test = sub.add_parser("test", help="Build and run engine tests")
    _add_engine_config_arguments(test, allow_release=False)
    test.add_argument("--filter", metavar="REGEX", help="Run matching CTest tests only")
    test.add_argument("--no-build", action="store_true", help="Run tests without building first")
    test.add_argument("--jobs", type=int, metavar="N", help="Parallel build jobs")
    test.add_argument(
        "--log-level",
        default=_env("TEST_LOG_LEVEL", "debug"),
        choices=["debug", "info", "warn", "error"],
        help="HORO_LOG_LEVEL used by tests (default: debug)",
    )
    test.set_defaults(func=cmd_test)

    run = sub.add_parser("run", help="Build and run an engine application")
    run_sub = run.add_subparsers(dest="run_command", required=True)

    launcher = run_sub.add_parser("launcher", help="Build and run HoroEditor")
    _add_engine_config_arguments(launcher)
    launcher.add_argument("--no-build", action="store_true", help="Run the existing binary")
    launcher.add_argument("--jobs", type=int, metavar="N", help="Parallel build jobs")
    launcher.add_argument(
        "launcher_args",
        nargs=argparse.REMAINDER,
        help="Arguments passed to HoroEditor; place them after --",
    )
    launcher.set_defaults(func=cmd_run_launcher)

    run_editor = run_sub.add_parser("editor", help="Run the starter app in editor mode")
    run_editor.add_argument(
        "--config",
        help="Starter build directory name (default: platform debug preset)",
    )
    run_editor.set_defaults(func=cmd_run_editor)

    run_play = run_sub.add_parser("play", help="Run the starter app in play mode")
    run_play.add_argument(
        "--config",
        help="Starter build directory name (default: platform debug preset)",
    )
    run_play.set_defaults(func=cmd_run_play)

    ui = sub.add_parser("ui", help="Run launcher UI tests")
    ui_sub = ui.add_subparsers(dest="ui_command", required=True)
    ui_test = ui_sub.add_parser("test", help="Build and run launcher tests")
    _add_engine_config_arguments(ui_test, allow_release=False)
    ui_test.add_argument(
        "--windowed",
        action="store_true",
        help="Run windowed UI automation instead of launcher unit tests",
    )
    ui_test.add_argument("--filter", help="Windowed UI scenario filter")
    ui_test.add_argument("--no-build", action="store_true", help="Run without building first")
    ui_test.add_argument("--jobs", type=int, metavar="N", help="Parallel build jobs")
    ui_test.add_argument(
        "--capture",
        choices=["0", "1"],
        default=_env("UI_TEST_CAPTURE", "0"),
        help="Capture UI test screenshots (default: 0)",
    )
    ui_test.add_argument(
        "--delay-ms",
        default=_env("UI_TEST_DELAY_MS", "0"),
        help="Delay between UI automation actions in milliseconds",
    )
    ui_test.add_argument(
        "--output-dir",
        default=_env("UI_TEST_OUTPUT_DIR", str(REPO_ROOT / "ui_test_output")),
        help="UI test artifact output directory",
    )
    ui_test.add_argument(
        "--log-level",
        default=_env("TEST_LOG_LEVEL", "debug"),
        choices=["debug", "info", "warn", "error"],
        help="HORO_LOG_LEVEL used by UI tests",
    )
    ui_test.set_defaults(func=cmd_ui_test)

    project = sub.add_parser("project", help="Manage projects through horo-engine CLI")
    project_sub = project.add_subparsers(dest="project_command", required=True)

    project_create = project_sub.add_parser("create", help="Create a project")
    _add_project_path(project_create)
    project_create.add_argument(
        "--name",
        help="Project name (default: HORO_PROJECT_NAME or directory name)",
    )
    project_create.set_defaults(func=cmd_project_create)

    project_build = project_sub.add_parser("build", help="Build a project")
    _add_project_path(project_build)
    _add_project_config(project_build)
    project_build.add_argument("--dry-run", action="store_true")
    project_build.set_defaults(func=cmd_project_build)

    project_release = project_sub.add_parser("release", help="Create a project release")
    _add_project_path(project_release)
    _add_project_config(project_release)
    _add_release_arguments(project_release)
    project_release.add_argument("--output-root", help="Release output root override")
    project_release.add_argument("--dry-run", action="store_true")
    project_release.set_defaults(func=cmd_project_release)

    project_open = project_sub.add_parser("open", help="Open a project in HoroEditor")
    _add_project_path(project_open)
    project_open.add_argument("--dry-run", action="store_true")
    project_open.set_defaults(func=cmd_project_open)

    release_smoke = project_sub.add_parser(
        "release-smoke",
        help="Create, release, and boot-check a packaged project",
    )
    _add_project_path(release_smoke)
    _add_project_config(release_smoke)
    _add_release_arguments(release_smoke)
    release_smoke.add_argument("--name", help="Project name")
    release_smoke.add_argument(
        "--run-seconds",
        default="2",
        help="Seconds the packaged binary must stay alive",
    )
    release_smoke.add_argument(
        "--no-clean",
        dest="clean",
        action="store_false",
        help="Reuse the smoke project directory",
    )
    release_smoke.set_defaults(func=cmd_project_release_smoke, clean=True)

    coverage = sub.add_parser("coverage", help="Generate source coverage")
    coverage_sub = coverage.add_subparsers(dest="coverage_command", required=True)
    coverage_report = coverage_sub.add_parser("report", help="Generate an HTML coverage report")
    coverage_report.set_defaults(func=cmd_coverage)
    coverage_summary = coverage_sub.add_parser("summary", help="Print source coverage summary")
    coverage_summary.set_defaults(func=cmd_coverage_source_summary)

    clean = sub.add_parser("clean", help="Remove generated build directories")
    _add_engine_config_arguments(clean)
    clean.add_argument("--all", action="store_true", help="Remove the entire build directory")
    clean.set_defaults(func=cmd_clean)

    format_command = sub.add_parser("format", help="Format tracked C++ sources")
    format_command.add_argument("--check", action="store_true", help="Check without modifying files")
    format_command.set_defaults(func=cmd_format)

    cli = sub.add_parser("cli", help="Interact with the built horo-engine CLI")
    cli_sub = cli.add_subparsers(dest="cli_command", required=True)
    cli_help = cli_sub.add_parser("help", help="Build and print horo-engine CLI help")
    cli_help.set_defaults(func=cmd_cli_help)

    mcp = sub.add_parser("mcp", help="MCP development helpers")
    mcp_sub = mcp.add_subparsers(dest="mcp_command", required=True)
    release_example = mcp_sub.add_parser(
        "release-example",
        help="Print a project.release MCP payload example",
    )
    _add_project_path(release_example)
    _add_project_config(release_example)
    _add_release_arguments(release_example)
    release_example.set_defaults(func=cmd_mcp_release_example)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    if argv is None:
        argv = sys.argv[1:]
    if not argv:
        parser.print_help()
        return 0

    args = parser.parse_args(argv)
    return args.func(args)

if __name__ == "__main__":
    raise SystemExit(main())
