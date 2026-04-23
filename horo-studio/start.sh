#!/usr/bin/env bash
# Horo Studio — development startup script.
#
# First-time setup (browser / dev mode):
#   ./start.sh --setup
#
# Electron desktop setup (builds packaged app with electron-builder):
#   ./start.sh --setup-electron
#   ./start.sh --electron          (open previously built app)
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
  echo "==> Building Theia (browser mode)..."
  yarn theia build --mode development
  echo "==> Setup complete. Run './start.sh' to launch at http://localhost:3000."
  exit 0
fi

if [[ "$1" == "--setup-electron" ]]; then
  echo "==> Rebuilding native modules for Electron ABI..."
  node_modules/.bin/electron-rebuild -f
  echo "==> Restoring ripgrep binary..."
  node node_modules/@vscode/ripgrep/lib/postinstall.js
  echo "==> Building Theia (Electron target)..."
  NODE_OPTIONS="--max-old-space-size=4096" yarn theia build --app-target=electron --mode development
  echo "==> Packaging with electron-builder --dir..."
  NODE_OPTIONS="--max-old-space-size=4096" node_modules/.bin/electron-builder --dir
  # electron-builder computes the app.asar hash from an intermediate file; fix it now.
  echo "==> Fixing ASAR integrity hash in Info.plist..."
  node - << 'NODE_SCRIPT'
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const appDir = process.platform === 'darwin' ? 'dist/mac-arm64/HoroStudio.app' :
               process.platform === 'linux'  ? 'dist/linux-arm64-unpacked'     : 'dist/win-arm64-unpacked';

// Compute SHA256 of the final app.asar
const asarPath = path.join(appDir, 'Contents/Resources/app.asar');
if (!fs.existsSync(asarPath)) {
  console.log('No app.asar found at', asarPath, '— skipping hash fix');
  process.exit(0);
}
const hash = crypto.createHash('sha256').update(fs.readFileSync(asarPath)).digest('hex');

const plistPath = path.join(appDir, 'Contents/Info.plist');
if (!fs.existsSync(plistPath)) {
  console.log('No Info.plist found — skipping hash fix');
  process.exit(0);
}

// Update the hash with PlistBuddy (available on macOS)
try {
  execFileSync('/usr/libexec/PlistBuddy', [
    '-c', `Set :ElectronAsarIntegrity:Resources/app.asar:hash ${hash}`,
    plistPath
  ]);
  console.log('Updated ASAR integrity hash in Info.plist:', hash.substring(0,16) + '...');
  // Re-sign with ad-hoc to update the code signature
  execFileSync('codesign', ['--force', '--deep', '--sign', '-', appDir], { stdio: 'inherit' });
} catch (e) {
  console.error('Hash fix warning:', e.message);
}
NODE_SCRIPT
  echo ""
  echo "==> Electron build complete. Launch with: ./start.sh --electron"
  exit 0
fi

if [[ "$1" == "--electron" ]]; then
  echo "==> Quitting any running HoroStudio instance..."
  osascript -e 'quit app "HoroStudio"' 2>/dev/null || true
  sleep 1

  echo "==> Clearing Theia layout storage..."
  rm -rf ~/Library/Application\ Support/horo-studio/IndexedDB/ 2>/dev/null || true
  rm -rf ~/Library/Application\ Support/horo-studio/Local\ Storage/ 2>/dev/null || true
  rm -rf ~/Library/Application\ Support/horo-studio/Session\ Storage/ 2>/dev/null || true

  echo "==> Launching Horo Studio (Electron desktop)..."
  # Unset ELECTRON_RUN_AS_NODE if set (it would make Electron act as plain Node.js).
  unset ELECTRON_RUN_AS_NODE
  if [[ "$(uname)" == "Darwin" ]]; then
    open -n dist/mac-arm64/HoroStudio.app
  elif [[ "$(uname)" == "Linux" ]]; then
    dist/linux-arm64-unpacked/horo-studio
  else
    start "" "dist/win-arm64-unpacked/HoroStudio.exe"
  fi
  exit 0
fi

# Ensure ripgrep binary exists (it gets wiped by some rebuilds)
if [ ! -f "node_modules/@vscode/ripgrep/bin/rg" ]; then
  echo "[start.sh] Restoring ripgrep binary..."
  node node_modules/@vscode/ripgrep/lib/postinstall.js
fi

echo "==> Starting Horo Studio at http://localhost:3000"
exec yarn theia start --port 3000 --plugins=local-dir:plugins "$@"
