#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# package-linux.sh — Create a Linux distributable tar.gz for HoroEngine.
#
# Usage:
#   scripts/package-linux.sh --build-dir <dir> --output-dir <dir>
#                            [--version <ver>] [--install-script]
#                            [--desktop-file] [--appimage] [--help]
#
# Minimum deliverable: Directory with HoroEditor, horopak, SDK, and bundled
# shared libraries.  Optional: install.sh, .desktop file, AppImage.
#
# Dependencies on the build host: cmake, ldd, patchelf (recommended),
# tar, gzip.  Falls back to LD_LIBRARY_PATH wrapper when patchelf is absent.
#
# Verification (manual / CI):
#   1. Extract the tar.gz on a clean Linux container.
#   2. Run ./bin/HoroEditor — must launch without missing-library errors.
#   3. Run ldd on the binary — only system libs (linux-vdso, libc, libm,
#      libdl, libpthread, ld-linux, libstdc++, libgcc_s, libwayland-*,
#      libX11, libGL, etc.) should reference system paths.  Everything
#      else must resolve inside the package.
# ---------------------------------------------------------------------------
set -euo pipefail

# ---- Defaults --------------------------------------------------------------
BUILD_DIR=""
OUTPUT_NAME="HoroEngine-linux-x86_64"
VERSION=""
WITH_INSTALL_SCRIPT="no"
WITH_DESKTOP_FILE="no"
WITH_APPIMAGE="no"
DRY_RUN="no"

# ---- Help ------------------------------------------------------------------
usage() {
    sed -n '1,15p' "$0" | tail -n +2
    echo ""
    echo "Options:"
    echo "  --build-dir DIR       CMake build directory (required)"
    echo "  --output-dir DIR      Output directory for the package (required)"
    echo "  --version VER         Version string for metadata"
    echo "  --install-script      Generate install.sh"
    echo "  --desktop-file        Generate .desktop file"
    echo "  --appimage            (NYI) Reserved for AppImage support"
    echo "  --dry-run             Print planned steps without executing"
    echo "  -h, --help            Show this help"
    exit 0
}

# ---- Argument parsing ------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)       BUILD_DIR="$2"; shift 2 ;;
        --output-dir)      OUTPUT_DIR="$2"; shift 2 ;;
        --version)         VERSION="$2"; shift 2 ;;
        --install-script)  WITH_INSTALL_SCRIPT="yes"; shift ;;
        --desktop-file)    WITH_DESKTOP_FILE="yes"; shift ;;
        --appimage)        WITH_APPIMAGE="yes"; shift ;;
        --dry-run)         DRY_RUN="yes"; shift ;;
        -h|--help)         usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

# ---- Validation ------------------------------------------------------------
if [[ -z "$BUILD_DIR" || -z "$OUTPUT_DIR" ]]; then
    echo "ERROR: --build-dir and --output-dir are required."
    usage
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "ERROR: Build directory does not exist: $BUILD_DIR"
    exit 1
fi

DIST_DIR="$OUTPUT_DIR"

# ---- Tools -----------------------------------------------------------------
PATCHELF=""
if command -v patchelf &>/dev/null; then
    PATCHELF="patchelf"
fi

# ---- Banner ----------------------------------------------------------------
echo "=== HoroEngine Linux Package Builder ==="
echo "  Build dir   : $BUILD_DIR"
echo "  Output dir  : $OUTPUT_DIR"
echo "  Version     : ${VERSION:-<detect>}"
echo "  patchelf    : ${PATCHELF:-<not found — will use wrapper script>}"
echo "  Features    : install-script=$WITH_INSTALL_SCRIPT desktop=$WITH_DESKTOP_FILE"
echo ""

if [[ "$DRY_RUN" == "yes" ]]; then
    echo "[DRY RUN] Would stage to $DIST_DIR"
    exit 0
fi

# Ensure output directory exists
mkdir -p "$DIST_DIR"

# ---- Helpers ---------------------------------------------------------------

# Directories whose .so files are considered system libraries and should
# NOT be bundled into the package.
SYSTEM_LIB_DIRS=(
    /lib /lib64 /usr/lib /usr/lib64
    /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu
    /usr/local/lib
)

is_system_lib() {
    local lib="$1"
    for sysdir in "${SYSTEM_LIB_DIRS[@]}"; do
        if [[ "$lib" == $sysdir/* ]]; then
            return 0
        fi
    done
    return 1
}

# Collect ELF executables from a directory tree.
find_elf_binaries() {
    local dir="$1"
    find "$dir" -type f -executable -print0 2>/dev/null || true
}

# ---- Step 1: Scan build-tree binaries for non-system shared libs ------------
echo "[1/6] Scanning build-tree binaries for shared library dependencies ..."

declare -A BUNDLED_LIBS  # soname -> absolute path on build host

# Collect build-tree binaries (before cmake --install strips/changes RPATH).
BUILD_BINARIES=()
while IFS= read -r -d '' elf; do
    BUILD_BINARIES+=("$elf")
done < <(find_elf_binaries "$BUILD_DIR/bin")

if [[ ${#BUILD_BINARIES[@]} -eq 0 ]]; then
    echo "WARNING: No ELF binaries found in $BUILD_DIR/bin"
fi

for binary in "${BUILD_BINARIES[@]}"; do
    if [[ ! -f "$binary" ]]; then
        continue
    fi

    while IFS= read -r line; do
        # ldd output format: "libfoo.so.X => /path/to/libfoo.so.X (0x...)"
        if [[ "$line" =~ ^[[:space:]]*([^[:space:]]+)[[:space:]]*=\>[[:space:]]*([^[:space:]]+) ]]; then
            soname="${BASH_REMATCH[1]}"
            libpath="${BASH_REMATCH[2]}"

            # Skip virtual DSOs.
            if [[ "$libpath" == "linux-vdso"* ]] || [[ "$libpath" == *"ld-linux"* ]]; then
                continue
            fi

            # Skip system libraries.
            if is_system_lib "$libpath"; then
                continue
            fi

            # Track first-seen path per soname.
            if [[ -z "${BUNDLED_LIBS[$soname]:-}" ]]; then
                BUNDLED_LIBS[$soname]="$libpath"
                echo "  [found] $soname => $libpath"
            fi
        fi
    done < <(ldd "$binary" 2>/dev/null || true)
done

SCANNED_COUNT=${#BUNDLED_LIBS[@]}
if (( SCANNED_COUNT == 1 )); then
    echo "  1 non-system shared library identified."
else
    echo "  $SCANNED_COUNT non-system shared libraries identified."
fi

# ---- Step 2: Stage via cmake --install -------------------------------------
echo "[2/6] Installing via cmake --install ..."
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR" --strip

# ---- Step 3: Detect version ------------------------------------------------
if [[ -z "$VERSION" ]]; then
    # POSIX sed — portable across GNU, musl/BusyBox, and macOS.
    VERSION=$(sed -n 's/^HORO_ENGINE_VERSION:STRING=//p' \
        "$BUILD_DIR/CMakeCache.txt" 2>/dev/null || true)
    VERSION="${VERSION:-0.0.0}"
fi
echo "[3/6] Version resolved: $VERSION"

# ---- Step 4: Copy bundled libs into staging & set RPATH ---------------------
echo "[4/6] Bundling shared libraries and setting RPATH ..."

if [[ ${#BUNDLED_LIBS[@]} -gt 0 ]]; then
    mkdir -p "$DIST_DIR/lib"
    for soname in "${!BUNDLED_LIBS[@]}"; do
        src="${BUNDLED_LIBS[$soname]}"
        if [[ -f "$src" ]]; then
            cp -v "$src" "$DIST_DIR/lib/"
        else
            echo "WARNING: Source for $soname not found at $src — skipping"
        fi
    done
fi

# Collect all staging binaries for RPATH patching.
STAGING_BINARIES=()
while IFS= read -r -d '' elf; do
    STAGING_BINARIES+=("$elf")
done < <(find_elf_binaries "$DIST_DIR/bin")

# Also include any .so files we just copied into lib/ (they may need
# RPATH for recursive dependencies).
while IFS= read -r -d '' so; do
    STAGING_BINARIES+=("$so")
done < <(find "$DIST_DIR/lib" -maxdepth 1 -type f -name '*.so*' -print0 2>/dev/null || true)

if [[ -n "$PATCHELF" ]]; then
    for binary in "${STAGING_BINARIES[@]}"; do
        if [[ -f "$binary" ]] && file "$binary" | grep -q ELF; then
            # Set RPATH relative to binary: bin/HoroEditor → $ORIGIN/../lib
            "$PATCHELF" --set-rpath '$ORIGIN/../lib' "$binary" 2>/dev/null || true
        fi
    done
    echo "  RPATH set to \$ORIGIN/../lib via patchelf (${#STAGING_BINARIES[@]} ELF files)"
else
    # Fallback: create LD_LIBRARY_PATH wrapper scripts for bin/ executables.
    for binary in "${STAGING_BINARIES[@]}"; do
        if [[ ! -f "$binary" ]] || ! file "$binary" | grep -q ELF; then
            continue
        fi
        # Only wrap files directly in bin/.
        if [[ "$(dirname "$binary")" != "$DIST_DIR/bin" ]]; then
            continue
        fi
        base=$(basename "$binary")
        if [[ -f "$DIST_DIR/bin/$base" ]]; then
            mv "$DIST_DIR/bin/$base" "$DIST_DIR/bin/.${base}.bin"
            cat > "$DIST_DIR/bin/$base" <<'WRAPPER'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$DIR/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$DIR/.${0##*/}.bin" "$@"
WRAPPER
            chmod +x "$DIST_DIR/bin/$base"
        fi
    done
    echo "  No patchelf — created LD_LIBRARY_PATH wrapper scripts"
fi

# ---- Step 5: Optional .desktop file ----------------------------------------
if [[ "$WITH_DESKTOP_FILE" == "yes" ]]; then
    echo "[5a/6] Generating .desktop file ..."
    cat > "$DIST_DIR/${OUTPUT_NAME}.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=HoroEngine Editor
Comment=HoroEngine Game Engine Editor v${VERSION}
Exec=/opt/horo-engine/bin/HoroEditor
Icon=/opt/horo-engine/share/icons/horo.png
Terminal=false
Categories=Development;IDE;
DESKTOP
    echo "  Wrote ${OUTPUT_NAME}.desktop"
fi

# ---- Step 5b: Optional install.sh ------------------------------------------
if [[ "$WITH_INSTALL_SCRIPT" == "yes" ]]; then
    echo "[5b/6] Generating install.sh ..."
    cat > "$DIST_DIR/install.sh" <<INSTALLER
#!/usr/bin/env bash
# HoroEngine v${VERSION} — Linux installer
set -euo pipefail

DEFAULT_PREFIX="/opt/horo-engine"
PREFIX="\${HORO_PREFIX:-\$DEFAULT_PREFIX}"

echo "Installing HoroEngine v${VERSION} to \$PREFIX ..."
mkdir -p "\$PREFIX"
cp -r "\$(dirname "\$0")/." "\$PREFIX/"

if [[ -f "\$PREFIX/${OUTPUT_NAME}.desktop" ]]; then
    mkdir -p "\$HOME/.local/share/applications"
    cp "\$PREFIX/${OUTPUT_NAME}.desktop" "\$HOME/.local/share/applications/"
    echo "  Desktop entry installed."
fi

echo "HoroEngine installed to \$PREFIX"
echo "Run: \$PREFIX/bin/HoroEditor"
INSTALLER
    chmod +x "$DIST_DIR/install.sh"
    echo "  Wrote install.sh"
fi

# ---- Step 6: Summary -------------------------------------------------------
echo ""
echo "=== Package Complete ==="
echo "  Output  : $OUTPUT_DIR"
echo "  Layout  :"
find "$DIST_DIR" -maxdepth 2 -not -path '*/\.*' | sort | sed 's|'"$DIST_DIR"'|   |'
echo ""
echo "=== Verification Checklist ==="
echo "  1. Check shared library linkage:"
echo "       ldd $OUTPUT_DIR/bin/HoroEditor  # all non-system libs must resolve inside ./lib/"
echo "  2. Launch the editor:"
echo "       $OUTPUT_DIR/bin/HoroEditor"
echo "  3. Run horopak:"
echo "       $OUTPUT_DIR/bin/horopak --help"

exit 0
