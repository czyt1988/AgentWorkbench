#!/usr/bin/env bash
# AgentLauncher Windows packaging script.
# Usage: bash scripts/package.sh  (or double-click in Explorer)
#
# Prerequisites:
#   - Qt 6.5+ (msvc build recommended) installed on this machine
#   - Visual Studio with MSVC C++ tools (auto-detected via vswhere)
#
# The Qt and Visual Studio paths are auto-detected. To force a specific
# install, set QT_PREFIX / VCVARS in the CONFIG section below.

set -euo pipefail

# ============================================================================
#  CONFIG — optional overrides; leave empty to auto-detect
# ============================================================================

# Qt installation prefix (must point to e.g. <Qt>/<version>/<compiler>).
# Leave empty to auto-detect the newest Qt 6.x msvc install on this machine.
QT_PREFIX=""

# Visual Studio vcvars64.bat path (used to set up MSVC env when not already
# loaded, e.g. when double-clicking the script). Leave empty to auto-detect
# via vswhere; it is skipped when MSVC is not needed or not found.
VCVARS=""

# Minimum Qt version accepted by the build (see CMakeLists.txt).
QT_MIN_VERSION="6.5"

# Build output and distribution directories
BUILD_DIR="build-release"
DIST_DIR="dist/AgentLauncher"

# Version number (used in the zip file name). Leave empty to read it
# automatically from the project() call in CMakeLists.txt.
VERSION=""

# ============================================================================
#  Qt / MSVC auto-detection
# ============================================================================

# True when $1 is an existing Qt prefix containing bin/windeployqt.exe.
qt_prefix_is_valid() {
    [[ -n "${1:-}" && -f "${1}/bin/windeployqt.exe" ]]
}

# Print the version component of a Qt prefix (<prefix> == <root>/<ver>/<compiler>).
qt_version_of() {
    basename "$(dirname "$1")"
}

# Succeed when version $1 is >= $2 (version-sort comparison).
version_at_least() {
    [[ -n "${1:-}" ]] || return 1
    local newest
    newest="$(printf '%s\n%s\n' "$2" "$1" | sort -V | tail -n 1)"
    [[ "$newest" == "$1" ]]
}

# Print one candidate Qt prefix per line for every
# <root>/<version>/<compiler>/bin/windeployqt.exe under $1.
qt_candidates_in() {
    local root="$1" version_dir compiler_dir
    [[ -d "$root" ]] || return 0
    for version_dir in "$root"/*/; do
        [[ -d "$version_dir" ]] || continue
        for compiler_dir in "$version_dir"*/; do
            [[ -d "$compiler_dir" ]] || continue
            [[ -f "${compiler_dir}bin/windeployqt.exe" ]] || continue
            printf '%s\n' "${compiler_dir%/}"
        done
    done
}

# Given candidate prefixes on stdin, print the best one:
# newest version first, preferring msvc builds over mingw.
pick_best_qt() {
    local line ver prio
    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        ver="$(qt_version_of "$line")"
        version_at_least "$ver" "$QT_MIN_VERSION" || continue
        case "$(basename "$line")" in
            msvc*) prio=0 ;;
            *)     prio=1 ;;
        esac
        printf '%s|%s|%s\n' "$prio" "$ver" "$line"
    done | sort -t '|' -k1,1n -k2,2V | awk -F '[|]' 'NR == 1 { print $3 }'
}

# Convert a Git-Bash/MSYS path (/c/foo/bar) to a Windows path (C:/foo/bar).
to_windows_path() {
    local p="$1" drive
    if [[ "$p" =~ ^/([a-zA-Z])/(.*)$ ]]; then
        drive="$(printf '%s' "${BASH_REMATCH[1]}" | tr '[:lower:]' '[:upper:]')"
        printf '%s:/%s\n' "$drive" "${BASH_REMATCH[2]}"
    else
        printf '%s\n' "$p"
    fi
}

# Auto-detect a Qt prefix, in priority order:
#   1. explicit $QT_PREFIX (if set and valid)
#   2. default location C:/Qt, then common locations (other drives, user installs)
#   3. broad search across every mounted drive
find_qt_prefix() {
    local root candidate roots up letter

    if [[ -n "${QT_PREFIX:-}" ]]; then
        if qt_prefix_is_valid "$QT_PREFIX"; then
            echo "Using Qt from QT_PREFIX: $QT_PREFIX" >&2
            printf '%s\n' "$(to_windows_path "$QT_PREFIX")"
            return 0
        fi
        echo "Warning: QT_PREFIX '$QT_PREFIX' has no bin/windeployqt.exe; trying auto-detection." >&2
    fi

    # Default + common roots, in priority order.
    roots=()
    roots+=( "C:/Qt" "D:/Qt" "E:/Qt" )
    if [[ -n "${USERPROFILE:-}" ]]; then
        up="${USERPROFILE//\\//}"
        up="${up%/}"
        [[ -n "$up" ]] && roots+=( "$up/Qt" )
    fi

    for root in "${roots[@]}"; do
        [[ -n "$root" ]] || continue
        candidate="$(qt_candidates_in "$root" | pick_best_qt)"
        if [[ -n "$candidate" ]]; then
            candidate="$(to_windows_path "$candidate")"
            echo "Found Qt in $root: $candidate" >&2
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    # Broad search — every mounted drive's top-level Qt folder first, then a
    # bounded find for windeployqt.exe as a last resort.
    echo "Qt not found in common locations; searching all drives..." >&2
    for letter in {c..z}; do
        [[ -d "/$letter" ]] || continue
        candidate="$(qt_candidates_in "/$letter/Qt" | pick_best_qt)"
        if [[ -n "$candidate" ]]; then
            candidate="$(to_windows_path "$candidate")"
            echo "Found Qt on ${letter}:/Qt: $candidate" >&2
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    for letter in {c..z}; do
        [[ -d "/$letter" ]] || continue
        candidate="$(
            find "/$letter" -maxdepth 6 \
                \( -type d \( -name Windows -o -name ProgramData -o -name '$Recycle.Bin' \
                   -o -name 'System Volume Information' -o -name Recovery \) -prune \) -o \
                -type f -name 'windeployqt.exe' -path '*/bin/*' -print 2>/dev/null \
            | sed 's#/bin/windeployqt\.exe$##' \
            | pick_best_qt || true
        )"
        if [[ -n "$candidate" ]]; then
            candidate="$(to_windows_path "$candidate")"
            echo "Found Qt via deep search on drive ${letter}: $candidate" >&2
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

# Auto-detect vcvars64.bat: explicit $VCVARS first, then vswhere.
find_vcvars() {
    local vswhere vcvars

    if [[ -n "${VCVARS:-}" ]]; then
        if [[ -f "$VCVARS" ]]; then
            printf '%s\n' "$VCVARS"
            return 0
        fi
        echo "Warning: VCVARS '$VCVARS' not found; trying auto-detection." >&2
    fi

    for vswhere in \
        "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" \
        "C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"; do
        [[ -f "$vswhere" ]] || continue
        vcvars="$("$vswhere" -latest -products '*' \
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
            -find 'VC/Auxiliary/Build/vcvars64.bat' 2>/dev/null | awk 'NR == 1 { print }')"
        if [[ -n "$vcvars" ]]; then
            printf '%s\n' "${vcvars//\\//}"
            return 0
        fi
    done

    return 1
}

# ============================================================================
#  Main script — no need to edit below this line
# ============================================================================

# Keep the window open when double-clicked (works for both success and error)
trap 'echo ""; read -rp "Press Enter to close..."' EXIT

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

# Locate Qt (auto-detect, or use the QT_PREFIX override).
QT_PREFIX="$(find_qt_prefix || true)"
if [[ -z "$QT_PREFIX" ]]; then
    echo "Error: could not locate a Qt installation (looking for bin/windeployqt.exe)." >&2
    echo "Set QT_PREFIX in scripts/package.sh to your Qt path, e.g. C:/Qt/6.7.3/msvc2019_64." >&2
    exit 1
fi
echo "Using Qt prefix: $QT_PREFIX"

# Auto-load MSVC environment if dumpbin isn't on PATH (needed by windeployqt)
if ! command -v dumpbin &>/dev/null; then
    VCVARS="$(find_vcvars || true)"
    if [[ -n "$VCVARS" && -f "$VCVARS" ]]; then
        echo "Loading MSVC environment from $VCVARS ..."
        eval "$(cmd //c "\"$VCVARS\" >nul 2>nul && set" 2>/dev/null | sed -E 's/^([A-Za-z0-9_]+)=(.*)$/export \1="\2"/')"
    else
        echo "Warning: dumpbin not on PATH and no MSVC vcvars64.bat found; windeployqt may fail." >&2
    fi
fi

# A stale CMake cache left over after the project folder was moved or renamed
# makes configure fail with a source-mismatch error. Detect it and clear the
# build directory so configure can regenerate from scratch.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cached_src=""
    while IFS= read -r line; do
        [[ "$line" == CMAKE_HOME_DIRECTORY:INTERNAL=* ]] || continue
        cached_src="${line#CMAKE_HOME_DIRECTORY:INTERNAL=}"
        break
    done < "$BUILD_DIR/CMakeCache.txt"
    if [[ -n "$cached_src" ]]; then
        # Compare case-insensitively (Windows is case-insensitive and the
        # cache may store a different drive-letter case than $PWD). Both
        # values use forward slashes already, so only lowercase + strip a
        # trailing slash before comparing.
        cached_norm="${cached_src%/}"; cached_norm="${cached_norm,,}"
        if [[ "$PWD" =~ ^/([a-zA-Z])/(.*)$ ]]; then
            cur="${BASH_REMATCH[1]^^}:/${BASH_REMATCH[2]}"
        else
            cur="$PWD"
        fi
        cur="${cur%/}"; cur="${cur,,}"
        if [[ "$cached_norm" != "$cur" ]]; then
            echo "Stale CMake cache in $BUILD_DIR (generated for $cached_src); removing it and reconfiguring."
            rm -rf "$BUILD_DIR"
        fi
    fi
fi

echo "=== [1/4] Release build ==="
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR" --config Release

echo ""
echo "=== [2/4] Prepare dist directory ==="
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
# MSBuild (VS generator) puts Release output under $BUILD_DIR/Release/;
# Ninja puts it directly in $BUILD_DIR/. Handle both.
if [[ -f "$BUILD_DIR/Release/AgentLauncher.exe" ]]; then
    cp "$BUILD_DIR/Release/AgentLauncher.exe" "$DIST_DIR/"
else
    cp "$BUILD_DIR/AgentLauncher.exe" "$DIST_DIR/"
fi

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
