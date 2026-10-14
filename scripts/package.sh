#!/usr/bin/env bash
# AgentWorkbench Windows packaging script.
# Usage: bash scripts/package.sh  (or double-click in Explorer)
#
# Prerequisites:
#   - Qt 6.5+ (msvc build recommended) installed on this machine
#   - Visual Studio with MSVC C++ tools (auto-detected via vswhere)
#
# The compile step is delegated to scripts/build.sh, which owns Qt/MSVC
# detection and the build itself. This script adds the deployment steps:
# windeployqt, the MSVC C++ runtime, qt.conf, a completeness check and the
# portable zip. The build is configured without the test targets — a package
# needs the application only.
#
# The package has to run on a machine with neither Visual Studio nor the VC++
# redistributable installed, so the MSVC runtime DLLs are deployed app-locally:
# copied from <VC>/Redist/MSVC/<version>/x64/Microsoft.VC*.CRT into the package
# root, where the Windows loader looks first. windeployqt does not do that on
# its own — its --compiler-runtime only puts the redistributable *installer*
# into the package, which needs an administrator on every target machine.
#
# The last steps verify the result: every required file must be there, and every
# DLL the package imports must be in the package or provided by Windows. An
# incomplete package is exactly the kind that still starts on this machine and
# fails on a fresh one, so both checks are fatal — set AWB_ALLOW_INCOMPLETE=1 to
# package such a result anyway.
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
DIST_DIR="${DIST_DIR:-dist/AgentWorkbench}"

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
PROJECT_ROOT="$PWD"

# Set when an incomplete package was packaged anyway (stays 0 when the package is
# complete), so the final summary can say so.
WAIVED_INCOMPLETE=0

# Follow up on a problem that makes the package incomplete: stop, unless
# AWB_ALLOW_INCOMPLETE=1 accepted the result. The call site says what is wrong
# and what it means; this only decides whether it is fatal.
incomplete() {
    if [[ "${AWB_ALLOW_INCOMPLETE:-0}" == "1" ]]; then
        WAIVED_INCOMPLETE=1
        echo "AWB_ALLOW_INCOMPLETE=1 is set: packaging the incomplete result anyway." >&2
        return 0
    fi
    echo "Set AWB_ALLOW_INCOMPLETE=1 to package it anyway." >&2
    exit 1
}

# Convert a Git-Bash path (/d/foo) to the Windows form the MSVC tools expect
# (D:/foo). Only the import check hands paths to a native tool.
to_win_path() {
    local p="$1"
    if [[ "$p" =~ ^/([a-zA-Z])/(.*)$ ]]; then
        printf '%s:/%s\n' "${BASH_REMATCH[1]^^}" "${BASH_REMATCH[2]}"
    else
        printf '%s\n' "$p"
    fi
}

# Print the dumpbin.exe of the toolset the MSVC runtime came from. dumpbin ships
# the DLLs it needs next to itself, so it runs without the MSVC environment.
find_dumpbin() {
    local vc_root="${REDIST_CRT%%/Redist/MSVC/*}" toolset="" cand="" best=""
    [[ -n "$REDIST_CRT" && "$vc_root" != "$REDIST_CRT" ]] || return 1
    toolset="$(basename "$(dirname "$(dirname "$REDIST_CRT")")")"
    if [[ -f "$vc_root/Tools/MSVC/$toolset/bin/Hostx64/x64/dumpbin.exe" ]]; then
        printf '%s\n' "$vc_root/Tools/MSVC/$toolset/bin/Hostx64/x64/dumpbin.exe"
        return 0
    fi
    for cand in "$vc_root"/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe; do
        [[ -f "$cand" ]] && best="$cand"
    done
    [[ -n "$best" ]] || return 1
    printf '%s\n' "$best"
}

# Print the DLL names $1 imports, one per line. Load-time imports only: a
# delay-loaded DLL is allowed to be absent until it is first used.
imported_dlls() {
    MSYS2_ARG_CONV_EXCL='*' "$dumpbin" -dependents "$(to_win_path "$1")" 2>/dev/null \
        | awk '/Image has the following dependencies/ { insec = 1; got = 0; next }
               /Image has the following delay load/ { insec = 0; next }
               insec && /^[ \t]+[A-Za-z0-9_.-]+\.dll[ \t]*$/ {
                   gsub(/^[ \t]+|[ \t]+$/, ""); print; got = 1; next }
               insec && got && /^$/ { insec = 0 }'
}

# True when $1 (lowercase) is one of the MSVC runtime families that never come
# with Windows: they belong to the VC++ redistributable, so finding one in this
# machine's System32 says nothing about a clean-machine install — every machine
# that has Visual Studio or any VC++ application has them copied in there.
is_msvc_runtime_name() {
    case "$1" in
        msvcp*|vcruntime*|concrt*|vccorlib*|msvcr*|mfc*|vcomp*) return 0 ;;
    esac
    return 1
}

# Derive version from CMakeLists.txt unless VERSION was set explicitly above.
if [[ -z "${VERSION:-}" ]]; then
    VERSION="$(sed -nE 's/^project\([A-Za-z0-9_]+[[:space:]]+VERSION[[:space:]]+([0-9.]+).*/\1/p' CMakeLists.txt | head -n1)"
    if [[ -z "$VERSION" ]]; then
        echo "Error: could not read version from CMakeLists.txt; set VERSION in this script." >&2
        exit 1
    fi
fi
echo "Version: $VERSION"

ZIP_NAME="AgentWorkbench-${VERSION}-win64-Portable.zip"

echo ""
echo "=== [1/6] Release build (scripts/build.sh) ==="
# --no-tests keeps the unit test targets out of the release build: they are not
# part of the shipped package, so compiling them only costs time.
bash scripts/build.sh --release --no-tests --build-dir "$BUILD_DIR"

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
echo "=== [2/6] Prepare dist directory ==="
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
cp "$EXE" "$DIST_DIR/"

echo ""
echo "=== [3/6] windeployqt: pull Qt/QML dependencies ==="
"$QT_PREFIX/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler \
    --qmldir src "$DIST_DIR/AgentWorkbench.exe"

# Pin Qt's prefix to the exe directory. Without qt.conf, Qt relocates the
# prefix relative to Qt6Core.dll and guesses; an explicit qt.conf guarantees
# the deployed app always resolves plugins/qml modules from its own folder,
# never from a Qt installation on the target machine.
cat > "$DIST_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = .
EOF

echo ""
echo "=== [4/6] MSVC C++ runtime (app-local) ==="
# windeployqt deploys Qt only. Without the runtime DLLs the package starts here —
# where Visual Studio installed the VC++ redistributable — and fails on a fresh
# machine with "MSVCP140.dll was not found". Copy the CRT folder of the toolset
# that built this binary: app-local deployment is what Microsoft documents for
# exactly this, needs no installer or administrator, and wins over whatever
# runtime the target machine happens to have installed.
REDIST_CRT="$(bash scripts/build.sh --print-redist --build-dir "$BUILD_DIR" || true)"
CRT_DLLS=0
if [[ -n "$REDIST_CRT" && -d "$REDIST_CRT" ]]; then
    for dll in "$REDIST_CRT"/*.dll; do
        [[ -f "$dll" ]] || continue
        cp "$dll" "$DIST_DIR/"
        CRT_DLLS=$((CRT_DLLS + 1))
    done
fi
if [[ $CRT_DLLS -eq 0 ]]; then
    echo "Error: the MSVC C++ runtime could not be deployed, so this package would" >&2
    echo "       need the VC++ redistributable installed on every target machine." >&2
    echo "       'bash scripts/build.sh --print-redist' tells you what was missing." >&2
    incomplete
else
    echo "Copied $CRT_DLLS DLL(s) from $REDIST_CRT"
    echo "No VC++ redistributable needed on the machines this package runs on."
fi

echo ""
echo "=== [5/6] Verify the package ==="

# Everything the application cannot start without. The MSVC runtime is listed
# because windeployqt never deploys it — see step 4.
REQUIRED_FILES=(
    AgentWorkbench.exe
    qt.conf
    Qt6Core.dll
    Qt6Gui.dll
    Qt6Qml.dll
    Qt6Quick.dll
    Qt6Network.dll
    platforms/qwindows.dll
    msvcp140.dll
    vcruntime140.dll
    vcruntime140_1.dll
)
# The embedded web surface needs more than DLLs. Required only when the package
# contains that module at all (an AWB_ENABLE_WEBENGINE=OFF build does not), and
# accepts the spellings several Qt versions use for the helper process and the
# resource folder.
WEBENGINE_FILES=(
    Qt6WebEngineQuick.dll
    "QtWebEngineProcess.exe|WebEngineProcess.exe"
    "resources/icudtl.dat|icudtl.dat"
    "resources/qtwebengine_resources.pak|qtwebengine_resources.pak"
)

# True when at least one of the given paths, relative to the package, exists.
any_in_package() {
    local path
    for path in "$@"; do
        [[ -e "$DIST_DIR/$path" ]] && return 0
    done
    return 1
}

missing=()
for file in "${REQUIRED_FILES[@]}"; do
    [[ -e "$DIST_DIR/$file" ]] || missing+=( "$file" )
done
if [[ -e "$DIST_DIR/Qt6WebEngineCore.dll" ]]; then
    for file in "${WEBENGINE_FILES[@]}"; do
        # A "|" lists the accepted spellings for one file.
        IFS='|' read -r -a spellings <<<"$file"
        any_in_package "${spellings[@]}" || missing+=( "${file//|/ or }" )
    done
fi
if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Error: the package is missing files it cannot start without:" >&2
    for file in "${missing[@]}"; do
        echo "       $file" >&2
    done
    incomplete
else
    echo "All required files are present."
fi

# The loader looks in the executable's own directory first, so every DLL the
# package imports has to sit there — unless Windows itself provides it. Checking
# the imports rather than trusting a hand-written list is what catches the
# dependency nobody thought about: the package that prompted this check contained
# no MSVC runtime at all, and only the imports of its binaries said so.
dumpbin="$(find_dumpbin || true)"
if [[ -z "$dumpbin" ]]; then
    echo "Import check skipped: no dumpbin.exe found next to the MSVC runtime."
else
    # Windows is case-insensitive; ${name,,} folds both sides.
    declare -A in_package=()
    while IFS= read -r name; do
        in_package["${name,,}"]=1
    done < <(ls -1 "$DIST_DIR")
    system32="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}" 2>/dev/null || printf '/c/Windows')/System32"

    declare -A unresolved=()
    binaries=0
    while IFS= read -r file; do
        binaries=$((binaries + 1))
        while IFS= read -r name; do
            name="${name%$'\r'}"
            [[ -n "$name" ]] || continue
            lname="${name,,}"
            # API sets are loader contracts the OS resolves itself, never files.
            [[ "$lname" == api-ms-win-* ]] && continue
            [[ -n "${in_package[$lname]:-}" ]] && continue
            # The MSVC runtime has to be in the package even when this machine
            # has a copy in System32 — that copy is the redistributable, which
            # is not what a clean install looks like.
            if ! is_msvc_runtime_name "$lname" && [[ -f "$system32/$name" ]]; then
                continue
            fi
            unresolved["$lname"]="$name (imported by ${file#"$DIST_DIR"/})"
        done < <(imported_dlls "$file")
    done < <(find "$DIST_DIR" -type f \( -name '*.dll' -o -name '*.exe' \))

    if [[ ${#unresolved[@]} -eq 0 ]]; then
        echo "Checked $binaries binaries: every import is in the package, an API set"
        echo "or a DLL that ships with Windows."
    else
        echo "Error: these imports are not in the package:" >&2
        while IFS= read -r line; do
            echo "       $line" >&2
        done < <(printf '%s\n' "${unresolved[@]}" | sort)
        echo "       A machine that has them only because something else installed" >&2
        echo "       them (the VC++ redistributable, another application) fails there." >&2
        incomplete
    fi
fi

echo ""
echo "=== [6/6] Package as zip ==="
# Both branches have to put the dist folder itself at the archive root, and they
# disagree on how to get there: Compress-Archive adds the folder name on its own,
# while 7-Zip stores the paths as given and so has to run from that folder's
# parent. Replacing the archive first keeps 7-Zip from merging into a stale one
# and shipping files this build no longer has.
rm -f "dist/$ZIP_NAME"
if command -v 7z &>/dev/null; then
    ( cd "$(dirname "$DIST_DIR")" && 7z a -tzip "$PROJECT_ROOT/dist/$ZIP_NAME" "$(basename "$DIST_DIR")" )
else
    powershell -NoProfile -Command \
        "Compress-Archive -Path '$DIST_DIR' -DestinationPath 'dist/$ZIP_NAME' -Force"
fi

echo ""
echo "=== Done ==="
echo "Distribution directory: $DIST_DIR"
echo "Zip archive:            dist/$ZIP_NAME"
echo "MSVC runtime:           $CRT_DLLS DLL(s) bundled (app-local)"
if [[ $WAIVED_INCOMPLETE -eq 1 ]]; then
    echo ""
    echo "WARNING: this package is incomplete (see the warnings above). It may not"
    echo "         start on a machine that lacks what could not be deployed."
fi
