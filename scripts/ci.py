#!/usr/bin/env python3
"""CI helper entrypoint for JUnit patching and coverage exclusions."""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def _run(cmd: list[str], env: dict[str, str] | None = None, timeout: int = 120) -> None:
    run_env = dict(os.environ)
    if env:
        run_env.update(env)
    try:
        subprocess.run(
            cmd,
            env=run_env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=timeout,
        )
    except Exception:
        pass


def build_tag_map(binary: str) -> dict[str, str]:
    try:
        out = subprocess.check_output(
            [binary, "--list-tests"],
            stderr=subprocess.DEVNULL,
            timeout=30,
            text=True,
        )
    except Exception:
        return {}

    tag_map: dict[str, str] = {}
    lines = out.splitlines()
    i = 0
    while i < len(lines):
        m = re.match(r"^  (.+)$", lines[i])
        if m:
            tc = m.group(1)
            if i + 1 < len(lines) and re.match(r"^    \[", lines[i + 1]):
                tag_map[tc] = lines[i + 1].strip()
                i += 2
                continue
        i += 1
    return tag_map


def patch_xml(xml_path: str, stem: str, tag_map: dict[str, str]) -> None:
    try:
        txt = Path(xml_path).read_text(encoding="utf-8")
    except OSError:
        return

    txt = re.sub(r'classname="([^"]+)\.global"', r'classname="\1"', txt)
    txt = re.sub(r'classname="global"', f'classname="{stem}"', txt)

    def inject(m: re.Match) -> str:
        name = m.group(1)
        tags = tag_map.get(name, "")
        return f'<testcase name="{name} {tags}"' if tags else m.group(0)

    txt = re.sub(r'<testcase name="([^"]*)"', inject, txt)
    Path(xml_path).write_text(txt, encoding="utf-8")


def load_ctest_entries(builddir: str) -> dict[str, list[dict]]:
    try:
        result = subprocess.run(
            ["ctest", "--test-dir", builddir, "--show-only=json-v1"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        tests = json.loads(result.stdout).get("tests", [])
    except Exception:
        return {}

    by_stem: dict[str, list[dict]] = {}
    for test in tests:
        cmd = test.get("command", [])
        if not cmd:
            continue
        stem = Path(cmd[0]).stem
        entry: dict = {"name": test.get("name", stem), "args": cmd[1:], "env": {}}

        for prop in test.get("properties", []):
            if prop.get("name") != "ENVIRONMENT":
                continue
            raw_value = prop.get("value", [])
            if isinstance(raw_value, str):
                values = [item.replace(r"\;", ";") for item in re.split(r"(?<!\\);", raw_value) if item]
            elif isinstance(raw_value, list):
                values = [item for item in raw_value if isinstance(item, str)]
            else:
                values = []

            for ev in values:
                k, _, v = ev.partition("=")
                if k:
                    entry["env"][k] = v

        for ev in test.get("environment", []):
            k, _, v = ev.partition("=")
            if k:
                entry["env"][k] = v
        by_stem.setdefault(stem, []).append(entry)
    return by_stem


def cmd_junit(args: argparse.Namespace) -> int:
    outdir = args.outdir
    bindir = args.bindir
    builddir = args.builddir

    os.makedirs(outdir, exist_ok=True)
    ctest_by_stem = load_ctest_entries(builddir) if builddir else {}
    handled: set[str] = set()

    for stem, entries in ctest_by_stem.items():
        binary = os.path.join(bindir, stem)
        if not os.path.isfile(binary):
            binary = binary + ".exe"
        if not os.path.isfile(binary):
            continue

        needs_special = any(e["env"] or e["args"] for e in entries)
        if not needs_special:
            continue

        tag_map = build_tag_map(binary)
        for entry in entries:
            xml_path = os.path.join(outdir, entry["name"] + ".xml")
            _run([binary, f"--reporter=JUnit::out={xml_path}"] + entry["args"], env=entry["env"] or None)
            patch_xml(xml_path, stem, tag_map)

        handled.add(stem)

    pattern = os.path.join(bindir, "test_*")
    for binary in glob.glob(pattern):
        stem = Path(binary).stem
        if stem in handled:
            continue
        if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
            continue

        xml_path = os.path.join(outdir, stem + ".xml")
        _run([binary, f"--reporter=JUnit::out={xml_path}"])
        patch_xml(xml_path, stem, build_tag_map(binary))
    return 0


def load_coverage_patterns(repo_root: Path) -> list[str]:
    props = repo_root / "sonar-project.properties"
    for line in props.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("sonar.coverage.exclusions="):
            value = line.split("=", 1)[1]
            return [part.strip() for part in value.split(",") if part.strip()]
    return []


def path_regex(path: Path) -> str:
    normalized = str(path.resolve()).replace("\\", "/")
    return re.escape(normalized).replace("/", r"[\\/]")


def gcovr_path_regex(path: Path) -> str:
    """Return a regex matching @p path with forward-slash separators only.

    gcovr normalises filter targets to forward slashes before matching and
    rejects character classes like ``[\\/]`` with a "filters must use forward
    slashes" warning. Keep the regex straightforward for that consumer.
    """
    normalized = str(path.resolve()).replace("\\", "/")
    return re.escape(normalized)


def to_gcovr(patterns: list[str], repo_root: Path) -> list[str]:
    out: list[str] = []
    for pattern in patterns:
        if pattern.endswith("/**"):
            directory = gcovr_path_regex(repo_root / pattern[:-3])
            out.append(f"--exclude ^{directory}/.*$")
        else:
            exact = gcovr_path_regex(repo_root / pattern)
            out.append(f"--exclude ^{exact}$")
    return out


def to_lcov(patterns: list[str]) -> list[str]:
    out: list[str] = []
    for pattern in patterns:
        if pattern.endswith("/**"):
            dir_name = pattern[:-3].rstrip("/")
            out.append(f"'*/{dir_name}/*'")
        else:
            out.append(f"'*/{pattern}'")
    return out


def to_opencppcoverage(patterns: list[str], repo_root: Path) -> list[str]:
    out: list[str] = []
    root = str(repo_root.resolve())
    for pattern in patterns:
        if pattern.endswith("/**"):
            dir_name = pattern[:-3].rstrip("/")
            out.append(f'--excluded_sources "{root}\\{dir_name}\\*"')
        else:
            win_path = root + "\\" + pattern.replace("/", "\\")
            out.append(f'--excluded_sources "{win_path}"')
    return out


def cmd_coverage_exclusions(args: argparse.Namespace) -> int:
    tool = args.tool.lower()
    repo_root = Path(args.repo_root) if args.repo_root else Path(__file__).resolve().parents[1]
    patterns = load_coverage_patterns(repo_root)

    if tool == "gcovr":
        parts = to_gcovr(patterns, repo_root)
    elif tool == "lcov":
        parts = to_lcov(patterns)
    elif tool == "opencppcoverage":
        parts = to_opencppcoverage(patterns, repo_root)
    else:
        print(f"Unknown tool: {tool}", file=sys.stderr)
        return 1

    print(" ".join(parts))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_junit = sub.add_parser("junit")
    p_junit.add_argument("outdir")
    p_junit.add_argument("bindir")
    p_junit.add_argument("builddir", nargs="?")
    p_junit.set_defaults(func=cmd_junit)

    p_cov = sub.add_parser("coverage-exclusions")
    p_cov.add_argument("tool")
    p_cov.add_argument("repo_root", nargs="?")
    p_cov.set_defaults(func=cmd_coverage_exclusions)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
