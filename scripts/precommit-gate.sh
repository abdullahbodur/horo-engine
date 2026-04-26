#!/usr/bin/env bash

set -euo pipefail

echo "[pre-commit] Building debug targets..."
make build

echo "[pre-commit] Running engine unit/integration tests..."
make test

echo "[pre-commit] Running launcher unit tests..."
make ui-test

echo "[pre-commit] Running launcher UI automation tests..."
if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
  HORO_UI_TEST_FILTER='launcher/*' make ui-test-windowed
elif command -v xvfb-run >/dev/null 2>&1; then
  xvfb-run --auto-servernum --server-args="-screen 0 1920x1080x24" \
    env HORO_UI_TEST_FILTER='launcher/*' make ui-test-windowed
else
  echo "[pre-commit] ERROR: UI automation requires DISPLAY/WAYLAND_DISPLAY or xvfb-run." >&2
  echo "[pre-commit] Install xvfb (Linux) or run from a desktop session." >&2
  exit 1
fi

echo "[pre-commit] horo-engine build + unit/ui test gate passed."
