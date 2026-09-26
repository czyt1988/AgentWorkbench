#!/usr/bin/env bash
# AgentLauncher Windows packaging script.
# Usage: bash scripts/package.sh  (or double-click in Explorer)
#
# Prerequisites:
#   - Qt 6.5+ (msvc build recommended) installed on this machine
#   - Visual Studio with MSVC C++ tools (auto-detected via vswhere)
#
# The compile step is delegated to scripts/build.sh, which owns Qt/MSVC
# detection and the build itself. This script only adds the deployment steps:
# windeployqt, qt.conf and the portable zip.
#
# Qt and Visual Studio are auto-detected. To force a specific install, export
# QT_PREFIX (or edit it in the CONFIG section below).

set -euo pipefail

# ============================================================================
#  CONFIG — every value can also be set in the environment
# ============================================================================

# Qt installation prefix (must point to e.g. <Qt>/<version>/<compiler>).
# Leave empty to reuse the prefix of the build directory, or to auto-detect the
# newest Qt 6.x msvc install on this machine.
QT_PREFIX="${QT_PREFIX:-}"

# Release build directory (owned by scripts/build.sh)
BUILD_DIR="${BUILD_DIR:-build-release}"

# Distribution directory; its name is the top-level folder inside the zip
DIST_DIR="${DIST_DIR:-dist/AgentLauncher}"

# Version number (used in the zip file name). Leave empty to read it
# automatically from the project() call in CMakeLists.txt.
VERSION="${VERSION:-}"

# ============================================================================
#  Main script — no need to edit below this line
# ============================================================================

# Keep the window open when double-clicked (works for both success and error).
# Skipped when run non-interactively, e.g. from an agent or CI.
if [[ -t 0 ]]; then
    trap 'echo ""; read -rp "Press Enter to close..."' EXIT
fi

# Always run from the project root, regardless of where the script is invoked
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

# Derive version from CMakeLists.txt unless VERSION was set explicitly above.
if [[ -z "${VERSION:-}" ]]; then
    VERSION="$(sed -nE 's/^project\([A-Za-z0-9_]+[[:space:]]+VERSION[[:space:]]+([0-9.]+).*/\1/p' CMakeLists.txt | head -n1)"
    if [[ -z "$VERSION" ]]; then
        echo "Error: could not read version from CMakeLists.txt; set VERSION in this script." >&2
        exit 1
    fi
fi
echo "Version: $VERSION"

ZIP_NAME="AgentLauncher-${VERSION}-win64-Portable.zip"

echo ""
echo "=== [1/4] Release build (scripts/build.sh) ==="
bash scripts/build.sh --release --build-dir "$BUILD_DIR"

# Read both values back from build.sh so this script never second-guesses the
# toolchain: the Qt prefix comes from the build directory that was just
# configured, and the executable path from the generator that was used.
QT_PREFIX="$(bash scripts/build.sh --print-qt --build-dir "$BUILD_DIR")"
EXE="$(bash scripts/build.sh --print-exe --release --build-dir "$BUILD_DIR")"
if [[ ! -f "$EXE" ]]; then
    echo "Error: expected the build output at $EXE, but it is not there." >&2
    exit 1
fi
echo "Using Qt prefix: $QT_PREFIX"

echo ""
echo "=== [2/4] Prepare dist directory ==="
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
cp "$EXE" "$DIST_DIR/"

echo ""
echo "=== [3/4] windeployqt: pull Qt/QML dependencies ==="
"$QT_PREFIX/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler \
    --qmldir qml "$DIST_DIR/AgentLauncher.exe"

# Pin Qt's prefix to the exe directory. Without qt.conf, Qt relocates the
# prefix relative to Qt6Core.dll and guesses; an explicit qt.conf guarantees
# the deployed app always resolves plugins/qml modules from its own folder,
# never from a Qt installation on the target machine.
cat > "$DIST_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = .
EOF

echo ""
echo "=== [4/4] Package as zip ==="
# Use 7-Zip if available, otherwise fall back to PowerShell
if command -v 7z &>/dev/null; then
    7z a -tzip "dist/$ZIP_NAME" "$DIST_DIR"
else
    powershell -NoProfile -Command \
        "Compress-Archive -Path '$DIST_DIR' -DestinationPath 'dist/$ZIP_NAME' -Force"
fi

echo ""
echo "=== Done ==="
echo "Distribution directory: $DIST_DIR"
echo "Zip archive:            dist/$ZIP_NAME"
