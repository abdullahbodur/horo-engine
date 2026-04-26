#!/usr/bin/env python3
"""Generate and post-process per-scenario Catch2 JUnit XML reports.

Usage:
    patch_junit_tags.py <outdir> <bindir> [<builddir>]

Runs every test_* binary in <bindir> via Catch2's JUnit reporter and writes
results to <outdir>/*.xml.  When <builddir> is provided the script reads the
CTest configuration (via ``ctest --show-only=json-v1``) so that binaries
registered with per-invocation environment variables or tag filters (e.g.
MONOLITH_LOG_LEVEL=warning for logger tests) are executed correctly.

After generating the XML files the script:
  1. Normalises classname: strips Catch2's "global" placeholder and replaces
     it with the binary stem so tests are grouped by file in CI reports.
  2. Appends the Catch2 tag string (e.g. ``[editor][layer]``) to every
     <testcase name> so the full identity appears in the report table.
"""

from __future__ import annotations

import glob
import json
import os
import re
import subprocess
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _run(cmd: list[str], env: dict[str, str] | None = None,
         timeout: int = 120) -> None:
    """Run a subprocess, silencing stdout/stderr, ignoring non-zero exit."""
    run_env = dict(os.environ)
    if env:
        run_env.update(env)
    try:
        subprocess.run(
            cmd, env=run_env,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=timeout,
        )
    except Exception:
        pass


def build_tag_map(binary: str) -> dict[str, str]:
    """Run ``binary --list-tests`` and return {test_name: '[tag1][tag2]'}."""
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
        # Test names are indented with exactly 2 spaces; tags follow with 4.
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
    """Fix classnames and inject tags into a JUnit XML file in-place."""
    try:
        txt = open(xml_path).read()
    except OSError:
        return

    # Normalise classname: "foo.global" → "foo", bare "global" → binary stem.
    txt = re.sub(r'classname="([^"]+)\.global"', r'classname="\1"', txt)
    txt = re.sub(r'classname="global"', f'classname="{stem}"', txt)

    # Append tags to <testcase name="...">.
    def inject(m: re.Match) -> str:
        name = m.group(1)
        tags = tag_map.get(name, "")
        return f'<testcase name="{name} {tags}"' if tags else m.group(0)

    txt = re.sub(r'<testcase name="([^"]*)"', inject, txt)
    open(xml_path, "w").write(txt)


# ---------------------------------------------------------------------------
# CTest configuration
# ---------------------------------------------------------------------------

def load_ctest_entries(builddir: str) -> dict[str, list[dict]]:
    """
    Return a mapping of binary stem → list of CTest entry dicts.

    Each entry has:
      ``name``   – CTest test name (used as XML file stem)
      ``args``   – extra args passed to the binary (typically a tag filter)
      ``env``    – dict of env vars required by the test
    """
    try:
        result = subprocess.run(
            ["ctest", "--test-dir", builddir, "--show-only=json-v1"],
            capture_output=True, text=True, timeout=30,
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
        entry: dict = {
            "name": test.get("name", stem),
            "args": cmd[1:],
            "env": {},
        }
        for ev in test.get("environment", []):
            k, _, v = ev.partition("=")
            entry["env"][k] = v
        by_stem.setdefault(stem, []).append(entry)

    return by_stem


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <outdir> <bindir> [<builddir>]",
              file=sys.stderr)
        sys.exit(1)

    outdir = sys.argv[1]
    bindir = sys.argv[2]
    builddir = sys.argv[3] if len(sys.argv) > 3 else None

    os.makedirs(outdir, exist_ok=True)

    # Load CTest configuration when builddir is available.
    ctest_by_stem = load_ctest_entries(builddir) if builddir else {}

    handled: set[str] = set()

    # --- Binaries that CTest registers with per-entry env vars / tag filters ---
    for stem, entries in ctest_by_stem.items():
        binary = os.path.join(bindir, stem)
        if not os.path.isfile(binary):
            binary = binary + ".exe"
        if not os.path.isfile(binary):
            continue

        needs_special = any(e["env"] or e["args"] for e in entries)
        if not needs_special:
            continue  # fall through to the simple path below

        tag_map = build_tag_map(binary)
        for entry in entries:
            xml_path = os.path.join(outdir, entry["name"] + ".xml")
            _run(
                [binary, f"--reporter=JUnit::out={xml_path}"] + entry["args"],
                env=entry["env"] or None,
            )
            patch_xml(xml_path, stem, tag_map)

        handled.add(stem)

    # --- All other binaries: single invocation, no special env needed ---------
    pattern = os.path.join(bindir, "test_*")
    for binary in glob.glob(pattern):
        # Skip non-executable files and Windows PDB / lib files.
        stem = Path(binary).stem
        if stem in handled:
            continue
        if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
            continue

        xml_path = os.path.join(outdir, stem + ".xml")
        _run([binary, f"--reporter=JUnit::out={xml_path}"])
        patch_xml(xml_path, stem, build_tag_map(binary))


if __name__ == "__main__":
    main()
