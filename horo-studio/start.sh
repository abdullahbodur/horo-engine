#!/usr/bin/env bash
# Horo Studio — development startup script.
#
# Run this once after a fresh install, or whenever native modules break:
#   ./start.sh --setup
#
# Normal usage (launch browser dev server):
#   ./start.sh
#
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ "$1" == "--setup" ]]; then
  echo "==> Rebuilding native Electron modules..."
  node_modules/.bin/electron-rebuild -f
  echo "==> Restoring ripgrep binary..."
  node node_modules/@vscode/ripgrep/lib/postinstall.js
  echo "==> Building Theia..."
  yarn theia build --mode development
  echo "==> Setup complete."
  exit 0
fi

# Ensure ripgrep binary exists (it gets wiped by electron-rebuild)
if [ ! -f "node_modules/@vscode/ripgrep/bin/rg" ]; then
  echo "[start.sh] Restoring ripgrep binary..."
  node node_modules/@vscode/ripgrep/lib/postinstall.js
fi

exec yarn theia start --plugins=local-dir:plugins "$@"
