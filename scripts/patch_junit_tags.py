#!/usr/bin/env python3
"""Post-process Catch2 JUnit XML reports.

Usage:
    patch_junit_tags.py <outdir> <bindir>

For every *.xml in <outdir>:
  1. Runs the matching binary from <bindir> with --list-tests to build a
     test-name -> [tags] map.
  2. Strips Catch2's "global" placeholder from classname attributes, replacing
     it with the binary stem name so tests are grouped by file in the report.
  3. Appends the tag string (e.g. "[editor][layer]") to each <testcase name>.
"""

import os
import glob
import re
import sys
import subprocess


def build_tag_map(binary: str) -> dict[str, str]:
    """Run binary --list-tests and return {test_name: '[tag1][tag2]'} map."""
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
        # Test names are indented with exactly 2 spaces
        m = re.match(r"^  (.+)$", lines[i])
        if m:
            tc = m.group(1)
            # Tags follow on the next line, indented with 4 spaces
            if i + 1 < len(lines) and re.match(r"^    \[", lines[i + 1]):
                tag_map[tc] = lines[i + 1].strip()
                i += 2
                continue
        i += 1
    return tag_map


def patch_xml(xml_path: str, stem: str, tag_map: dict[str, str]) -> None:
    txt = open(xml_path).read()

    # Normalize classname: "foo.global" -> "foo", bare "global" -> binary stem
    txt = re.sub(r'classname="([^"]+)\.global"', r'classname="\1"', txt)
    txt = re.sub(r'classname="global"', f'classname="{stem}"', txt)

    # Append tags to each <testcase name="...">
    def inject(m: re.Match) -> str:
        name = m.group(1)
        tags = tag_map.get(name, "")
        return f'<testcase name="{name} {tags}"' if tags else m.group(0)

    txt = re.sub(r'<testcase name="([^"]*)"', inject, txt)
    open(xml_path, "w").write(txt)


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <outdir> <bindir>", file=sys.stderr)
        sys.exit(1)

    outdir, bindir = sys.argv[1], sys.argv[2]

    for xml_path in glob.glob(os.path.join(outdir, "*.xml")):
        stem = os.path.splitext(os.path.basename(xml_path))[0]
        binary = os.path.join(bindir, stem)

        # Windows: try .exe suffix if plain binary is absent
        if not os.path.isfile(binary):
            binary = binary + ".exe"

        tag_map = {}
        if os.path.isfile(binary) and os.access(binary, os.X_OK):
            tag_map = build_tag_map(binary)

        patch_xml(xml_path, stem, tag_map)


if __name__ == "__main__":
    main()
