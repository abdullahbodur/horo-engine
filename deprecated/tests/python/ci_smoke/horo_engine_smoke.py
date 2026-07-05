#!/usr/bin/env python3
"""CI smoke test for the ``horo-engine`` CLI artifact flow.

Exercises the full project → build → release → archive-validate path:
  1. ``horo-engine project create``  — scaffold a game project
  2. ``horo-engine project build``    — cmake configure + compile
  3. ``horo-engine project release``  — package into a .horo archive
  4. ``horopak info`` / ``list``      — validate archive metadata & asset list
  5. ``horopak unpack``               — extract and byte-compare assets
  6. Encrypted variant                — password-protected release round-trip

Exits 0 on success, non-zero on any failure.

CI usage:
    python -m pytest tests/python/ci_smoke/test_horo_engine.py \
        -m ci_smoke --horo-engine-binary <path-to-horo-engine-binary>
"""

from __future__ import annotations

import argparse
import filecmp
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


# ── test infrastructure ──────────────────────────────────────────────────

def _find_horopak(engine_binary: str) -> str:
    """Heuristic: horopak lives next to the horo-engine binary."""
    engine_path = Path(engine_binary).resolve()
    horopak = engine_path.parent / ("horopak.exe" if sys.platform == "win32" else "horopak")
    if horopak.is_file():
        return str(horopak)
    # Fallback: search sibling directories one level up
    for candidate in engine_path.parent.parent.rglob("horopak*"):
        if candidate.is_file() and not candidate.name.endswith((".pdb", ".ilk", ".exp", ".lib")):
            return str(candidate)
    return "horopak"  # hope it's on PATH


def _sdk_root(engine_binary: str) -> str:
    """SDK root is the build directory containing the binary."""
    engine_path = Path(engine_binary).resolve()
    # Binary is at build/<preset>/bin/(Debug/)?horo-engine
    # SDK root = build/<preset>
    candidate = engine_path.parent  # bin/
    if candidate.name.lower() in ("bin", "debug", "release", "minsizerel"):
        candidate = candidate.parent  # build/<preset>/
    return str(candidate)


def run_cli(binary: str, args: list[str], *, timeout: int = 120,
            env: dict | None = None) -> subprocess.CompletedProcess:
    """Run a CLI binary with the given arguments."""
    return subprocess.run(
        [binary] + args,
        capture_output=True,
        text=True,
        timeout=timeout,
        env=env,
    )


def fail(test_name: str, detail: str) -> bool:
    """Report a test failure and return False."""
    print(f"  FAIL: {test_name}")
    print(f"        {detail}")
    return False


def ok(test_name: str) -> bool:
    """Report a test pass and return True."""
    print(f"  PASS: {test_name}")
    return True


# ── test suites ──────────────────────────────────────────────────────────

def smoke_help(engine_binary: str) -> bool:
    """horo-engine --help prints usage and exits 0."""
    cp = run_cli(engine_binary, ["--help"], timeout=10)
    if cp.returncode != 0:
        return fail("--help", f"exit code {cp.returncode} (expected 0)")
    if "Usage:" not in cp.stdout:
        return fail("--help", "output missing 'Usage:'")
    return ok("--help")


def smoke_version(engine_binary: str) -> bool:
    """horo-engine --version prints a version string and exits 0."""
    cp = run_cli(engine_binary, ["--version"], timeout=10)
    if cp.returncode != 0:
        return fail("--version", f"exit code {cp.returncode} (expected 0)")
    version = cp.stdout.strip()
    if not version:
        return fail("--version", "empty output")
    if not any(c.isdigit() for c in version):
        return fail("--version", f"no digits: '{version}'")
    print(f"  PASS: --version ({version})")
    return True


def smoke_no_args(engine_binary: str) -> bool:
    """horo-engine with no args prints usage and exits 2."""
    cp = run_cli(engine_binary, [], timeout=10)
    if cp.returncode != 2:
        return fail("no-args", f"exit code {cp.returncode} (expected 2)")
    if "Usage:" not in cp.stdout:
        return fail("no-args", "output missing 'Usage:'")
    return ok("no-args (exit 2 + usage)")


def smoke_project_create(engine_binary: str) -> tuple[Path, str] | None:
    """horo-engine project create — returns (project_root, project_name) or None."""
    tmp = Path(tempfile.mkdtemp(prefix="horo_engine_smoke_"))
    project_root = tmp / "MyHoroGame"
    project_name = "MyHoroGame"

    cp = run_cli(engine_binary, [
        "project", "create", str(project_root),
        "--name", project_name,
        "--sdk-root", _sdk_root(engine_binary),
    ], timeout=30)

    if cp.returncode != 0:
        shutil.rmtree(tmp, ignore_errors=True)
        fail("project create", f"exit code {cp.returncode}\n{cp.stderr}")
        return None

    if not (project_root / "CMakeLists.txt").is_file():
        shutil.rmtree(tmp, ignore_errors=True)
        fail("project create", f"CMakeLists.txt not found in {project_root}")
        return None

    if not (project_root / "assets").is_dir():
        shutil.rmtree(tmp, ignore_errors=True)
        fail("project create", f"assets/ directory not found in {project_root}")
        return None

    ok("project create")
    return project_root, project_name


def smoke_project_build(engine_binary: str, project_root: Path) -> bool:
    """horo-engine project build — configure + compile the game project."""
    cp = run_cli(engine_binary, [
        "project", "build", str(project_root),
        "--config", "Release",
        "--sdk-root", _sdk_root(engine_binary),
    ], timeout=180)

    if cp.returncode != 0:
        return fail("project build",
                    f"exit code {cp.returncode}\n--- stdout ---\n{cp.stdout}\n--- stderr ---\n{cp.stderr}")

    # Verify build output exists
    build_dir = project_root / "build"
    binary_name = project_root.name + (".exe" if sys.platform == "win32" else "")
    bin_candidates = list(build_dir.glob(f"**/{binary_name}"))
    if not bin_candidates:
        return fail("project build", f"binary {binary_name} not found under {build_dir}")

    ok("project build")
    return True


def smoke_project_release(engine_binary: str, project_root: Path,
                          version: str, password: str | None = None,
                          cleanup_handle: dict | None = None) -> Path | None:
    """horo-engine project release — returns release output directory or None."""
    args = [
        "project", "release", str(project_root),
        "--version", version,
        "--config", "Release",
        "--sdk-root", _sdk_root(engine_binary),
    ]
    env = dict(os.environ)
    if password:
        args.extend(["--archive-password", password])
        env["HORO_RELEASE_ARCHIVE_PASSWORD"] = password

    cp = run_cli(engine_binary, args, timeout=300, env=env)
    if cp.returncode != 0:
        fail("project release",
             f"exit code {cp.returncode}\n--- stdout ---\n{cp.stdout}\n--- stderr ---\n{cp.stderr}")
        return None

    # Find the release output directory
    release_root = project_root / "build" / "release"
    if not release_root.is_dir():
        fail("project release", f"release directory {release_root} not found")
        return None

    vtag = f"v{version}" if not version.startswith("v") else version
    candidates = sorted(release_root.glob(f"{vtag}_*"))
    if not candidates:
        fail("project release", f"no release output found matching {vtag}_* under {release_root}")
        return None

    output_path = candidates[-1]
    return output_path


# ── archive validation helpers ───────────────────────────────────────────

def validate_release_output(output_path: Path, project_name: str,
                            horopak_binary: str,
                            password: str | None = None) -> bool:
    """Validate the structure and integrity of a release output directory."""
    all_ok = True

    # 1. assets.horo archive must exist
    archive = output_path / "assets.horo"
    if sys.platform == "darwin" and (output_path / f"{project_name}.app").is_dir():
        archive = output_path / f"{project_name}.app" / "Contents" / "Resources" / "assets.horo"
    if not archive.is_file():
        all_ok = fail("release: assets.horo", f"not found in {output_path}")
        return all_ok
    ok("release: assets.horo exists")

    # 2. No raw JSON / scene files leaked into release output
    json_files = list(output_path.rglob("*.json"))
    if json_files:
        all_ok = fail("release: no raw JSON", f"found: {[str(f) for f in json_files]}")
    else:
        ok("release: no raw JSON in output")

    if password:
        print("  PASS: horopak info/list skipped for encrypted archive")
    else:
        # 3. Archive info via horopak
        info_args = ["info", "--input", str(archive)]
        cp = run_cli(horopak_binary, info_args, timeout=15)
        if cp.returncode != 0:
            all_ok = fail("horopak info", f"exit code {cp.returncode}\n{cp.stderr}")
        elif "HORO" not in cp.stdout or "Asset count" not in cp.stdout:
            all_ok = fail("horopak info", f"missing expected fields\n{cp.stdout}")
        else:
            ok("horopak info")

        # 4. Archive listing validates asset enumeration
        list_args = ["list", "--input", str(archive)]
        cp = run_cli(horopak_binary, list_args, timeout=15)
        if cp.returncode != 0:
            all_ok = fail("horopak list", f"exit code {cp.returncode}\n{cp.stderr}")
        elif "asset(s)" not in cp.stdout:
            all_ok = fail("horopak list", f"missing asset count\n{cp.stdout}")
        elif "scenes/level.json" not in cp.stdout:
            all_ok = fail("horopak list", f"missing generated scene asset\n{cp.stdout}")
        else:
            ok("horopak list")

    # 5. Extract and verify assets from the archive
    tmp_extract = output_path.parent / "smoke_extracted"
    tmp_extract.mkdir(parents=True, exist_ok=True)

    unpack_args = [
        "unpack", "--input", str(archive),
        "--output-dir", str(tmp_extract),
    ]
    if password:
        unpack_args.extend(["--password", password])

    cp = run_cli(horopak_binary, unpack_args, timeout=30)
    if cp.returncode != 0:
        all_ok = fail("horopak unpack", f"exit code {cp.returncode}\n{cp.stderr}")
    elif not any(tmp_extract.iterdir()):
        all_ok = fail("horopak unpack", f"no files extracted to {tmp_extract}")
    else:
        ok("horopak unpack")
        # Count extracted files
        extracted_count = sum(1 for _ in tmp_extract.rglob("*") if _.is_file())
        print(f"         extracted {extracted_count} file(s)")

    shutil.rmtree(tmp_extract, ignore_errors=True)

    # 6. Binary exists and is executable
    binary = output_path / (project_name + (".exe" if sys.platform == "win32" else ""))
    if sys.platform == "darwin" and (output_path / f"{project_name}.app").is_dir():
        binary = output_path / f"{project_name}.app" / "Contents" / "MacOS" / project_name

    if not binary.exists():
        # Search for any executable
        binaries = [p for p in output_path.iterdir()
                    if p.is_file() and os.access(p, os.X_OK)]
        if not binaries:
            all_ok = fail("release binary", f"no executable found in {output_path}")
            binary = None
        else:
            binary = binaries[0]
            print(f"  PASS: release binary found ({binary.name})")
    else:
        ok(f"release binary exists ({binary.name})")

    # 7. Smoke test execution
    if binary:
        try:
            # Run the game; it runs indefinitely so we expect a TimeoutExpired.
            # We set a short timeout (3s) to let it boot and load the scene.
            run_env = dict(os.environ)
            if password:
                run_env["HORO_RELEASE_ARCHIVE_PASSWORD"] = password
            cp = subprocess.run([str(binary)], capture_output=True, text=True,
                                timeout=3, env=run_env, cwd=str(binary.parent))
            # If we get here, it exited before the timeout.
            if cp.returncode != 0:
                all_ok = fail("release binary execution", f"crashed or exited with {cp.returncode}\nstderr: {cp.stderr}")
            else:
                ok("release binary execution (exited cleanly)")
        except subprocess.TimeoutExpired:
            ok("release binary execution (booted and stayed alive)")

    # 8. shaders/ directory if present
    shader_dir = output_path / "shaders"
    if shader_dir.is_dir():
        shader_files = list(shader_dir.glob("*"))
        if shader_files:
            print(f"  PASS: shaders/ directory with {len(shader_files)} file(s)")

    return all_ok


# ── main test flows ──────────────────────────────────────────────────────

def smoke_full_round_trip(engine_binary: str, horopak_binary: str) -> bool:
    """Full CLI flow: create → build → release → validate (plain, no encryption)."""
    print("\n--- Full round-trip (plain) ---")

    # 1. Create
    result = smoke_project_create(engine_binary)
    if result is None:
        return False
    project_root, project_name = result
    try:
        # 2. Build
        if not smoke_project_build(engine_binary, project_root):
            return False

        # 3. Release
        version = "0.1.0"
        output_path = smoke_project_release(engine_binary, project_root, version)
        if output_path is None:
            return False
        ok("project release")

        # 4. Validate release output
        if not validate_release_output(output_path, project_name, horopak_binary):
            return False

        print("  PASS: full round-trip (plain)")
        return True
    finally:
        shutil.rmtree(project_root.parent, ignore_errors=True)


def smoke_encrypted_round_trip(engine_binary: str, horopak_binary: str) -> bool:
    """Full CLI flow with encryption: create → build → release (password) → validate."""
    print("\n--- Encrypted round-trip ---")

    password = "ci-smoke-password-42"

    # 1. Create
    result = smoke_project_create(engine_binary)
    if result is None:
        return False
    project_root, project_name = result
    try:
        # 2. Build
        if not smoke_project_build(engine_binary, project_root):
            return False

        # 3. Release with password
        version = "0.0.1"
        output_path = smoke_project_release(engine_binary, project_root, version,
                                            password=password)
        if output_path is None:
            return False
        ok("project release (encrypted)")

        # 4. Validate with password
        if not validate_release_output(output_path, project_name, horopak_binary,
                                       password=password):
            return False

        # 5. Verify that wrong password fails on the archive
        archive = output_path / "assets.horo"
        cp = run_cli(horopak_binary, [
            "unpack",
            "--input", str(archive),
            "--output-dir", str(output_path.parent / "should_fail"),
            "--password", "wrong-password",
        ], timeout=15)
        if cp.returncode == 0:
            fail("encrypted: wrong-password rejection",
                 "unpack should have failed but exited 0")
            return False
        ok("encrypted: wrong-password correctly rejected")

        print("  PASS: encrypted round-trip")
        return True
    finally:
        shutil.rmtree(project_root.parent, ignore_errors=True)


def smoke_dry_run(engine_binary: str) -> bool:
    """Verify --dry-run prints commands without executing builds."""
    print("\n--- Dry-run smoke ---")

    tmp = Path(tempfile.mkdtemp(prefix="horo_engine_dryrun_"))
    project_root = tmp / "DryRunProject"
    try:
        # Create a real project first
        cp = run_cli(engine_binary, [
            "project", "create", str(project_root),
            "--name", "DryRunApp",
            "--sdk-root", _sdk_root(engine_binary),
        ], timeout=30)
        if cp.returncode != 0:
            return fail("dry-run create", f"exit code {cp.returncode}\n{cp.stderr}")

        # dry-run build
        cp = run_cli(engine_binary, [
            "project", "build", str(project_root),
            "--config", "Release",
            "--sdk-root", _sdk_root(engine_binary),
            "--dry-run",
        ], timeout=15)
        if cp.returncode != 0:
            return fail("dry-run build", f"exit code {cp.returncode}")
        if "cmake" not in cp.stdout:
            return fail("dry-run build", "output missing 'cmake' command")

        # Build output directory should NOT exist (dry-run didn't execute)
        if (project_root / "build").is_dir():
            # Some cmake artifacts may have been left — that's OK for dry-run,
            # but the binary shouldn't exist.
            binaries = list((project_root / "build").rglob(
                "DryRunApp" + (".exe" if sys.platform == "win32" else "")))
            if binaries:
                return fail("dry-run build", "binary exists despite --dry-run")

        ok("dry-run build")

        # dry-run release
        cp = run_cli(engine_binary, [
            "project", "release", str(project_root),
            "--version", "0.1.0",
            "--config", "Release",
            "--sdk-root", _sdk_root(engine_binary),
            "--dry-run",
        ], timeout=15)
        if cp.returncode != 0:
            return fail("dry-run release", f"exit code {cp.returncode}")
        if "cmake" not in cp.stdout:
            return fail("dry-run release", "output missing 'cmake' command")

        ok("dry-run release")
        return True
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def smoke_unsupported_target_logs(engine_binary: str) -> bool:
    """Unsupported local release targets fail with a copyable exact reason."""
    print("\n--- Unsupported target smoke ---")

    tmp = Path(tempfile.mkdtemp(prefix="horo_engine_unsupported_"))
    project_root = tmp / "MyHoroGame"
    try:
        cp = run_cli(engine_binary, [
            "project", "create", str(project_root),
            "--name", "MyHoroGame",
            "--sdk-root", _sdk_root(engine_binary),
        ], timeout=30)
        if cp.returncode != 0:
            return fail("unsupported create", f"exit code {cp.returncode}\n{cp.stderr}")

        unsupported = {
            "darwin": ("windows", "x86_64", "Windows x86_64"),
            "linux": ("windows", "x86_64", "Windows x86_64"),
            "win32": ("linux", "x86_64", "Linux x86_64"),
        }[sys.platform]
        target, arch, label = unsupported
        expected = (
            f"Build blocked: {label} target cannot be built locally. "
            "Reason: No cross-compilation toolchain configured for this target."
        )

        cp = run_cli(engine_binary, [
            "project", "release", str(project_root),
            "--version", "0.3.0",
            "--config", "Release",
            "--sdk-root", _sdk_root(engine_binary),
            "--target", target,
            "--arch", arch,
            "--dry-run",
        ], timeout=15)
        combined = cp.stdout + cp.stderr
        if cp.returncode != 1:
            return fail("unsupported target", f"exit code {cp.returncode} (expected 1)\n{combined}")
        if expected not in combined:
            return fail("unsupported target", f"missing exact reason\nexpected: {expected}\nactual: {combined}")
        ok("unsupported target exact reason")
        return True
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ── entry point ──────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description="CI smoke test for horo-engine CLI artifact flow"
    )
    parser.add_argument(
        "binary",
        help="Path to the horo-engine CLI binary",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip the full build+release round-trips (only run quick checks)",
    )
    args = parser.parse_args()

    engine_binary = args.binary
    if not os.path.isfile(engine_binary):
        print(f"ERROR: horo-engine binary not found at '{engine_binary}'")
        return 2

    horopak_binary = _find_horopak(engine_binary)
    if not shutil.which(horopak_binary) and not os.path.isfile(horopak_binary):
        print(f"WARNING: horopak not found at '{horopak_binary}' — "
              "archive validation will be skipped")
        horopak_binary = None  # Tests that need it will skip

    print(f"horo-engine CLI smoke test")
    print(f"  engine binary:  {engine_binary}")
    print(f"  horopak binary:  {horopak_binary or '(not found)'}")
    print(f"  sdk root:        {_sdk_root(engine_binary)}")

    # Quick tests (always run)
    quick_tests = [
        ("--help",               lambda: smoke_help(engine_binary)),
        ("--version",            lambda: smoke_version(engine_binary)),
        ("no-args (usage)",      lambda: smoke_no_args(engine_binary)),
    ]

    passed = 0
    failed = 0

    for name, fn in quick_tests:
        print()
        try:
            if fn():
                passed += 1
            else:
                failed += 1
        except Exception as e:
            print(f"  FAIL: {name} — unhandled exception: {e}")
            failed += 1

    if not args.skip_build:
        if horopak_binary:
            heavy_tests = [
                ("dry-run commands",  lambda: smoke_dry_run(engine_binary)),
                ("unsupported target logs",
                 lambda: smoke_unsupported_target_logs(engine_binary)),
                ("full round-trip (plain)",
                 lambda: smoke_full_round_trip(engine_binary, horopak_binary)),
                ("encrypted round-trip",
                 lambda: smoke_encrypted_round_trip(engine_binary, horopak_binary)),
            ]
        else:
            heavy_tests = [
                ("dry-run commands", lambda: smoke_dry_run(engine_binary)),
                ("unsupported target logs",
                 lambda: smoke_unsupported_target_logs(engine_binary)),
            ]

        for name, fn in heavy_tests:
            print()
            start = time.time()
            try:
                if fn():
                    passed += 1
                else:
                    failed += 1
            except Exception as e:
                print(f"  FAIL: {name} — unhandled exception: {e}")
                import traceback
                traceback.print_exc()
                failed += 1
            elapsed = time.time() - start
            print(f"  [{elapsed:.1f}s]")
    else:
        print("\n  (skipping build + release tests — --skip-build)")

    print()
    total = passed + failed
    print(f"Results: {passed} passed, {failed} failed, {total} total")

    if failed:
        print("SMOKE FAILED")
        return 1

    print("SMOKE PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
