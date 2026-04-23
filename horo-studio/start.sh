#!/usr/bin/env bash
# Compatibility shim: delegate to the cross-platform Node launcher.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec node "$SCRIPT_DIR/scripts/studio-cli.js" "$@"

