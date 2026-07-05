from __future__ import annotations

import os
from pathlib import Path

import pytest

from . import horopak_smoke

"""
Pytest-native coverage for horopak smoke behavior.

The fast tests use a temporary fake horopak executable so the smoke assertions can
run before the C++ build exists. The ci_smoke-marked tests run the same checks
against a real compiled binary when CI passes --horopak-binary.
"""


@pytest.fixture()
def fake_horopak(tmp_path: Path) -> str:
    """Create an executable fake horopak binary with deterministic archive behavior."""
    binary = tmp_path / "fake_horopak.py"
    binary.write_text(
        """
#!/usr/bin/env python3
from __future__ import annotations

import shutil
import sys
from pathlib import Path


def _arg_value(args, flag):
    return args[args.index(flag) + 1]


def main() -> int:
    args = sys.argv[1:]
    if args == ["--help"]:
        print("Usage: horopak <command>")
        return 0
    if args == ["--version"]:
        print("0.2.0-test")
        return 0
    if not args:
        print("Usage: horopak <command>")
        return 1

    command = args[0]
    if command == "pack":
        project_root = Path(_arg_value(args, "--project-root"))
        output = Path(_arg_value(args, "--output"))
        output.write_bytes(b"HORO fake archive")
        sidecar = output.with_suffix(output.suffix + ".contents")
        if sidecar.exists():
            shutil.rmtree(sidecar)
        shutil.copytree(project_root, sidecar)
        if "--password" in args:
            password = _arg_value(args, "--password")
            output.with_suffix(output.suffix + ".password").write_text(password)
            print("Encryption: enabled")
        return 0

    if command == "list":
        archive = Path(_arg_value(args, "--input"))
        sidecar = archive.with_suffix(archive.suffix + ".contents")
        for path in sorted(p for p in sidecar.rglob("*") if p.is_file()):
            print(path.relative_to(sidecar).as_posix())
        return 0

    if command == "info":
        archive = Path(_arg_value(args, "--input"))
        sidecar = archive.with_suffix(archive.suffix + ".contents")
        count = sum(1 for p in sidecar.rglob("*") if p.is_file())
        print("HORO fake archive")
        print(f"Asset count: {count}")
        return 0

    if command == "unpack":
        archive = Path(_arg_value(args, "--input"))
        output_dir = Path(_arg_value(args, "--output-dir"))
        password_file = archive.with_suffix(archive.suffix + ".password")
        if password_file.exists():
            if "--password" not in args or _arg_value(args, "--password") != password_file.read_text():
                print("wrong password", file=sys.stderr)
                return 2
        sidecar = archive.with_suffix(archive.suffix + ".contents")
        if output_dir.exists():
            shutil.rmtree(output_dir)
        shutil.copytree(sidecar, output_dir)
        return 0

    print(f"unknown command: {command}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
""".lstrip()
    )
    os.chmod(binary, 0o755)
    return str(binary)


@pytest.mark.script
@pytest.mark.parametrize(
    ("smoke_name", "function_name"),
    [
        ("help", "smoke_help"),
        ("version", "smoke_version"),
        ("no_args", "smoke_no_args"),
        ("round_trip", "smoke_round_trip"),
        ("encrypted_round_trip", "smoke_encrypted_round_trip"),
    ],
    ids=["help", "version", "no-args", "round-trip", "encrypted-round-trip"],
)
def test_horopak_smoke_checks_accept_valid_cli(fake_horopak, smoke_name, function_name):
    """Each horopak smoke check accepts a CLI preserving the expected archive contract."""
    check = getattr(horopak_smoke, function_name)

    assert check(fake_horopak), smoke_name


@pytest.mark.ci_smoke
@pytest.mark.parametrize(
    ("smoke_name", "function_name"),
    [
        ("help", "smoke_help"),
        ("version", "smoke_version"),
        ("no_args", "smoke_no_args"),
        ("round_trip", "smoke_round_trip"),
        ("encrypted_round_trip", "smoke_encrypted_round_trip"),
    ],
    ids=["help", "version", "no-args", "round-trip", "encrypted-round-trip"],
)
def test_horopak_binary_smoke(horopak_binary: Path, smoke_name, function_name):
    """Run pytest-native horopak smoke checks against the compiled CI artifact."""
    check = getattr(horopak_smoke, function_name)

    assert check(str(horopak_binary)), smoke_name
