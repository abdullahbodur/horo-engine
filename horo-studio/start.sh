#!/usr/bin/env bash
# Horo Studio — development startup script.
#
# First-time setup (browser / dev mode):
#   ./start.sh --setup
#
# Electron desktop setup (requires electron binary):
#   ./start.sh --setup-electron
#
# Normal launch (opens http://localhost:3000 in your browser):
#   ./start.sh
#
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ "$1" == "--setup" ]]; then
  echo "==> Rebuilding native modules for Node.js (browser/server mode)..."
  # Rebuild all native addons against the current system Node.js ABI.
  # This is the correct approach for 'theia start' (non-Electron) dev mode.
  npm rebuild --build-from-source
  echo "==> Restoring ripgrep binary..."
  node node_modules/@vscode/ripgrep/lib/postinstall.js
  echo "==> Building Theia (picks up freshly rebuilt native modules)..."
  yarn theia build --mode development
  echo "==> Setup complete. Run './start.sh' to launch."
  exit 0
fi

if [[ "$1" == "--setup-electron" ]]; then
  echo "==> Rebuilding native modules for Electron (desktop mode)..."
  node_modules/.bin/electron-rebuild -f
  echo "==> Restoring ripgrep binary..."
  node node_modules/@vscode/ripgrep/lib/postinstall.js
  echo "==> Building Theia..."
  yarn theia build --mode development
  echo "==> Electron setup complete. Run: yarn start:electron"
  exit 0
fi

# Ensure ripgrep binary exists (it gets wiped by some rebuilds)
if [ ! -f "node_modules/@vscode/ripgrep/bin/rg" ]; then
  echo "[start.sh] Restoring ripgrep binary..."
  node node_modules/@vscode/ripgrep/lib/postinstall.js
fi

echo "==> Starting Horo Studio at http://localhost:3000"
exec yarn theia start --port 3000 --plugins=local-dir:plugins "$@"
