#!/usr/bin/env python3
"""CI smoke test for the ``horopak`` CLI archive tool.

Exercises the full pack → list → info → unpack → byte-compare round-trip,
plus an encrypted round-trip with password-derived AES-256-CTR encryption.
Exits 0 on success, non-zero on any failure.

CI usage:
    python -m pytest tests/python/ci_smoke/test_horopak.py \
        -m ci_smoke --horopak-binary <path-to-horopak-binary>
"""

from __future__ import annotations

import argparse
import filecmp
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def run_horopak(binary: str, args: list[str]) -> subprocess.CompletedProcess:
    """Run the horopak binary with the given arguments."""
    return subprocess.run(
        [binary] + args,
        capture_output=True,
        text=True,
        timeout=30,
    )


def smoke_help(binary: str) -> bool:
    """horopak --help prints usage and exits 0."""
    cp = run_horopak(binary, ["--help"])
    if cp.returncode != 0:
        print(f"  FAIL: --help exit code {cp.returncode} (expected 0)")
        return False
    if "Usage:" not in cp.stdout:
        print("  FAIL: --help output missing 'Usage:'")
        return False
    if "horopak" not in cp.stdout:
        print("  FAIL: --help output missing 'horopak'")
        return False
    print("  PASS: --help")
    return True


def smoke_version(binary: str) -> bool:
    """horopak --version prints version and exits 0."""
    cp = run_horopak(binary, ["--version"])
    if cp.returncode != 0:
        print(f"  FAIL: --version exit code {cp.returncode} (expected 0)")
        return False
    version = cp.stdout.strip()
    if not version:
        print("  FAIL: --version produced empty output")
        return False
    # Version should be a semver or similar tag (e.g. 0.0.1, v0.0.1)
    if not any(c.isdigit() for c in version):
        print(f"  FAIL: --version output contains no digits: '{version}'")
        return False
    print(f"  PASS: --version ({version})")
    return True


def smoke_no_args(binary: str) -> bool:
    """horopak with no arguments prints help and exits 1."""
    cp = run_horopak(binary, [])
    if cp.returncode != 1:
        print(f"  FAIL: no-args exit code {cp.returncode} (expected 1)")
        return False
    if "Usage:" not in cp.stdout:
        print("  FAIL: no-args output missing 'Usage:'")
        return False
    print("  PASS: no-args (exit 1 + usage)")
    return True


def smoke_round_trip(binary: str) -> bool:
    """pack → list → info → unpack → byte-compare (unencrypted)."""
    tmp = Path(tempfile.mkdtemp(prefix="horopak_smoke_"))
    try:
        # Create test assets
        assets = tmp / "assets"
        subdir = assets / "subdir"
        subdir.mkdir(parents=True)
        (assets / "hello.txt").write_text("hello world\n")
        (assets / "data.bin").write_bytes(b"\x00\x01\x02\x03\x04\x05\x06\x07")
        (subdir / "nested.txt").write_text("nested file content\n")

        archive = tmp / "bundle.horo"

        # Pack
        cp = run_horopak(binary, [
            "pack",
            "--project-root", str(assets),
            "--output", str(archive),
        ])
        if cp.returncode != 0:
            print(f"  FAIL: pack exit code {cp.returncode}\n{cp.stderr}")
            return False
        if not archive.is_file():
            print("  FAIL: archive not created")
            return False
        print("  PASS: pack")

        # List
        cp = run_horopak(binary, ["list", "--input", str(archive)])
        if cp.returncode != 0:
            print(f"  FAIL: list exit code {cp.returncode}\n{cp.stderr}")
            return False
        if "hello.txt" not in cp.stdout or "data.bin" not in cp.stdout:
            print(f"  FAIL: list output missing expected assets\n{cp.stdout}")
            return False
        print("  PASS: list")

        # Info
        cp = run_horopak(binary, ["info", "--input", str(archive)])
        if cp.returncode != 0:
            print(f"  FAIL: info exit code {cp.returncode}\n{cp.stderr}")
            return False
        if "HORO" not in cp.stdout or "Asset count" not in cp.stdout:
            print(f"  FAIL: info output missing expected fields\n{cp.stdout}")
            return False
        print("  PASS: info")

        # Unpack
        extracted = tmp / "extracted"
        cp = run_horopak(binary, [
            "unpack",
            "--input", str(archive),
            "--output-dir", str(extracted),
        ])
        if cp.returncode != 0:
            print(f"  FAIL: unpack exit code {cp.returncode}\n{cp.stderr}")
            return False
        print("  PASS: unpack")

        # Byte-compare
        dircmp = filecmp.dircmp(assets, extracted)
        if dircmp.left_only or dircmp.right_only or dircmp.diff_files:
            print(f"  FAIL: byte-compare mismatch\n"
                  f"    left_only:  {dircmp.left_only}\n"
                  f"    right_only: {dircmp.right_only}\n"
                  f"    diff_files: {dircmp.diff_files}")
            return False
        print("  PASS: byte-compare")

        return True
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def smoke_encrypted_round_trip(binary: str) -> bool:
    """pack --password → unpack --password → byte-compare."""
    tmp = Path(tempfile.mkdtemp(prefix="horopak_smoke_crypto_"))
    try:
        assets = tmp / "assets"
        assets.mkdir(parents=True)
        (assets / "secret.txt").write_text("classified data\n")
        (assets / "binary.dat").write_bytes(bytes(range(256)))

        password = "ci-smoke-test-password-42"
        archive = tmp / "encrypted.horo"

        # Pack with encryption
        cp = run_horopak(binary, [
            "pack",
            "--project-root", str(assets),
            "--output", str(archive),
            "--password", password,
        ])
        if cp.returncode != 0:
            print(f"  FAIL: encrypted pack exit code {cp.returncode}\n{cp.stderr}")
            return False
        if "Encryption: enabled" not in cp.stdout:
            print(f"  FAIL: encrypted pack output missing 'Encryption: enabled'\n{cp.stdout}")
            return False
        print("  PASS: pack --password")

        # Unpack with correct password
        decrypted = tmp / "decrypted"
        cp = run_horopak(binary, [
            "unpack",
            "--input", str(archive),
            "--output-dir", str(decrypted),
            "--password", password,
        ])
        if cp.returncode != 0:
            print(f"  FAIL: encrypted unpack exit code {cp.returncode}\n{cp.stderr}")
            return False
        print("  PASS: unpack --password")

        # Byte-compare
        dircmp = filecmp.dircmp(assets, decrypted)
        if dircmp.left_only or dircmp.right_only or dircmp.diff_files:
            print(f"  FAIL: encrypted byte-compare mismatch\n"
                  f"    left_only:  {dircmp.left_only}\n"
                  f"    right_only: {dircmp.right_only}\n"
                  f"    diff_files: {dircmp.diff_files}")
            return False
        print("  PASS: encrypted byte-compare")

        # Verify wrong password fails
        cp = run_horopak(binary, [
            "unpack",
            "--input", str(archive),
            "--output-dir", str(tmp / "should_fail"),
            "--password", "wrong-password",
        ])
        if cp.returncode == 0:
            print("  FAIL: wrong-password unpack should have failed but exited 0")
            return False
        print("  PASS: wrong-password unpack correctly rejected")

        return True
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="CI smoke test for horopak CLI archive tool"
    )
    parser.add_argument(
        "binary",
        help="Path to the horopak binary",
    )
    args = parser.parse_args()

    binary = args.binary
    if not os.path.isfile(binary):
        print(f"ERROR: horopak binary not found at '{binary}'")
        return 2

    print(f"horopak CI smoke test")
    print(f"  binary: {binary}")
    print()

    tests = [
        ("--help",               smoke_help),
        ("--version",            smoke_version),
        ("no-args (usage)",      smoke_no_args),
        ("pack→list→info→unpack→compare", smoke_round_trip),
        ("encrypted round-trip", smoke_encrypted_round_trip),
    ]

    passed = 0
    failed = 0
    for name, fn in tests:
        try:
            if fn(binary):
                passed += 1
            else:
                failed += 1
        except Exception as e:
            print(f"  FAIL: {name} — unhandled exception: {e}")
            failed += 1

    print()
    print(f"Results: {passed} passed, {failed} failed, {len(tests)} total")

    if failed:
        print("SMOKE FAILED")
        return 1

    print("SMOKE PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
