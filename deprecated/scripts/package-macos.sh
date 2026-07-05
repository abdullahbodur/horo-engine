#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# package-macos.sh — Create a macOS .app bundle for HoroEngine.
#
# Usage:
#   scripts/package-macos.sh --build-dir <dir> --output-dir <dir> --app-name <name> [--dry-run]
#
# Creates a standard macOS .app bundle layout, copies the executable,
# bundles non-system dylibs via otool and install_name_tool, and leaves
# the Resources directory ready for horopak generation.
# ---------------------------------------------------------------------------
set -euo pipefail

BUILD_DIR=""
OUTPUT_DIR=""
APP_NAME=""
DRY_RUN="no"

usage() {
    echo "Usage: scripts/package-macos.sh --build-dir <dir> --output-dir <dir> --app-name <name> [--dry-run]"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --app-name)   APP_NAME="$2"; shift 2 ;;
        --dry-run)    DRY_RUN="yes"; shift ;;
        -h|--help)    usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

if [[ -z "$BUILD_DIR" || -z "$OUTPUT_DIR" || -z "$APP_NAME" ]]; then
    echo "ERROR: --build-dir, --output-dir, and --app-name are required."
    usage
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "ERROR: Build directory does not exist: $BUILD_DIR"
    exit 1
fi

APP_DIR="$OUTPUT_DIR/$APP_NAME.app"
MACOS_DIR="$APP_DIR/Contents/MacOS"
RESOURCES_DIR="$APP_DIR/Contents/Resources"
FRAMEWORKS_DIR="$APP_DIR/Contents/Frameworks"

echo "=== HoroEngine macOS Package Builder ==="
echo "  Build dir  : $BUILD_DIR"
echo "  Output dir : $OUTPUT_DIR"
echo "  App name   : $APP_NAME"
echo ""

if [[ "$DRY_RUN" == "yes" ]]; then
    echo "[DRY RUN] Would stage to $APP_DIR"
    exit 0
fi

# ---- Step 1: Create Layout -------------------------------------------------
echo "[1/4] Creating app bundle layout in $APP_DIR ..."
mkdir -p "$MACOS_DIR"
mkdir -p "$RESOURCES_DIR"
mkdir -p "$FRAMEWORKS_DIR"

cat > "$APP_DIR/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>$APP_NAME</string>
	<key>CFBundleIdentifier</key>
	<string>com.horoengine.$APP_NAME</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>$APP_NAME</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundleVersion</key>
	<string>1</string>
	<key>LSMinimumSystemVersion</key>
	<string>10.15</string>
</dict>
</plist>
PLIST

# ---- Step 2: Copy Executable -----------------------------------------------
echo "[2/4] Copying executables ..."
# Find primary executable in bin/. Assuming it's the largest or matching name.
# For simplicity, we copy all executables from bin/ and assume the one matching APP_NAME or HoroEditor is the main one.
MAIN_EXE=""
for f in "$BUILD_DIR/bin/"*; do
    if [[ -f "$f" && -x "$f" ]]; then
        cp "$f" "$MACOS_DIR/"
        # Rename HoroEditor to APP_NAME if it's the primary editor binary and APP_NAME is different
        if [[ "$(basename "$f")" == "HoroEditor" && "$APP_NAME" != "HoroEditor" ]]; then
            mv "$MACOS_DIR/HoroEditor" "$MACOS_DIR/$APP_NAME"
            MAIN_EXE="$MACOS_DIR/$APP_NAME"
        else
            MAIN_EXE="$MACOS_DIR/$(basename "$f")"
        fi
    fi
done

if [[ -z "$MAIN_EXE" ]]; then
    echo "ERROR: No executable found in $BUILD_DIR/bin/"
    exit 1
fi

# ---- Step 3: Bundle Dylibs -------------------------------------------------
echo "[3/4] Bundling dynamic libraries via otool ..."

is_system_lib() {
    local lib="$1"
    if [[ "$lib" == "/usr/lib/"* || "$lib" == "/System/Library/"* ]]; then
        return 0
    fi
    return 1
}

# Recursively resolve dylibs (simplified 1-level deep for now)
if command -v otool >/dev/null; then
    for binary in "$MACOS_DIR/"*; do
        if [[ -f "$binary" && -x "$binary" ]]; then
            while IFS= read -r line; do
                if [[ "$line" =~ ^[[:space:]]*([^[:space:]]+)[[:space:]]+\(compatibility ]]; then
                    libpath="${BASH_REMATCH[1]}"
                    if ! is_system_lib "$libpath"; then
                        libname=$(basename "$libpath")
                        if [[ ! -f "$FRAMEWORKS_DIR/$libname" ]]; then
                            echo "  Bundling $libname from $libpath"
                            if [[ -f "$libpath" ]]; then
                                cp "$libpath" "$FRAMEWORKS_DIR/"
                            elif [[ -f "$BUILD_DIR/lib/$libname" ]]; then
                                cp "$BUILD_DIR/lib/$libname" "$FRAMEWORKS_DIR/"
                            fi
                        fi
                        # Patch binary
                        install_name_tool -change "$libpath" "@executable_path/../Frameworks/$libname" "$binary" || true
                    fi
                fi
            done < <(otool -L "$binary" 2>/dev/null || true)
            # Add rpath for good measure
            install_name_tool -add_rpath "@executable_path/../Frameworks" "$binary" 2>/dev/null || true
        fi
    done
else
    echo "WARNING: otool not found, skipping dylib bundling."
fi

# ---- Step 4: Summary -------------------------------------------------------
echo "[4/4] Done."
echo "App bundle created at: $APP_DIR"
exit 0
