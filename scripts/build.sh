#!/usr/bin/env bash
# AgentLauncher build script — configure + compile in one command.
# Usage: bash scripts/build.sh [options]
#
# Prerequisites: Qt 6.5+ (msvc build recommended) and, for MSVC builds driven
# from Git Bash, Visual Studio with the C++ tools. Both are auto-detected.
#
# Unlike a bare `cmake -B build && cmake --build build`, this script loads the
# MSVC environment through a generated .bat wrapper. Importing vcvars64.bat into
# Git Bash by eval-ing `cmd /c ... set` does not work (cmd receives the escaped
# quotes literally), so the build would fail with "No CMAKE_CXX_COMPILER could
# be found".
#
# Options:
#   -d, --debug            Build Debug (default)
#   -r, --release          Build Release
#   -b, --build-dir DIR    Build directory
#                          (default: "build" for Debug, "build-release" for Release)
#   -j, --jobs N           Parallel compilation jobs (default: tool default)
#       --target NAME      Build one target only (AgentLauncher, AgentLauncherTests)
#   -t, --test             Run the unit tests (ctest) after building
#       --run              Launch the application after building (detached)
#   -c, --clean            Delete the build directory first (full rebuild)
#       --generator NAME   Force a CMake generator (default: keep whatever the
#                          build directory was configured with, else Ninja)
#       --qt PATH          Qt prefix override (same as QT_PREFIX=...)
#       --print-qt         Print the resolved Qt prefix and exit (detection only)
#       --print-exe        Print the built executable path and exit
#   -h, --help             Show this help
#
# Environment overrides: QT_PREFIX, VCVARS, GENERATOR, BUILD_DIR, JOBS,
#                        CMAKE_ARGS (extra configure arguments)
#
# `--` passes every remaining argument to the CMake configure step, e.g.
#   bash scripts/build.sh -- -DBUILD_TESTING=OFF

set -euo pipefail

# ============================================================================
#  CONFIG — defaults; override from the command line or the environment
# ============================================================================

# Configuration to build: Debug or Release.
CONFIG="Debug"

# Qt installation prefix (must point to e.g. <Qt>/<version>/<compiler>).
# Leave empty to reuse the prefix stored in the build directory, or to
# auto-detect the newest Qt 6.x msvc install on this machine.
QT_PREFIX="${QT_PREFIX:-}"

# Visual Studio vcvars64.bat path. Leave empty to auto-detect via vswhere.
VCVARS="${VCVARS:-}"

# CMake generator. Leave empty to keep the generator the build directory was
# configured with, or to prefer Ninja for a fresh build directory.
GENERATOR="${GENERATOR:-}"

# Minimum Qt version accepted by the build (see CMakeLists.txt).
QT_MIN_VERSION="6.5"

# ============================================================================
#  Qt / MSVC / Ninja detection
# ============================================================================

# True when $1 is an existing Qt prefix containing bin/windeployqt.exe.
qt_prefix_is_valid() {
    local p="${1//\\//}"
    [[ -n "$p" && -f "${p%/}/bin/windeployqt.exe" ]]
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

# Same, but with backslashes — for cmd built-ins like `cd /d` and `call`.
to_win_backslash_path() {
    local p
    p="$(to_windows_path "$1")"
    printf '%s\n' "${p//\//\\}"
}

# Resolve $1 (relative, MSYS-absolute or Windows-absolute) against the current
# directory and print it as a Windows path with forward slashes.
absolute_win_path() {
    local p="$1"
    if [[ "$p" =~ ^[a-zA-Z]:[/\\] ]]; then
        printf '%s\n' "${p//\\//}"
    elif [[ "$p" == /* ]]; then
        to_windows_path "$p"
    else
        printf '%s/%s\n' "$(to_windows_path "$PWD")" "${p#./}"
    fi
}

# Auto-detect a Qt prefix, in priority order:
#   1. explicit $QT_PREFIX / --qt (if set and valid)
#   2. the prefix the build directory was last configured with (if still valid)
#   3. default location C:/Qt, then common locations (other drives, user installs)
#   4. broad search across every mounted drive
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

# Print the Qt prefix this build would use: explicit override, else the one
# stored in the build directory, else auto-detection.
resolve_qt_prefix() {
    local cached entry

    if [[ -n "${QT_PREFIX:-}" ]]; then
        find_qt_prefix
        return $?
    fi

    cached="$(cache_value "$BUILD_DIR/CMakeCache.txt" CMAKE_PREFIX_PATH || true)"
    if [[ -n "$cached" ]]; then
        while IFS= read -r entry; do
            [[ -n "$entry" ]] || continue
            if qt_prefix_is_valid "$entry"; then
                echo "Using Qt from the existing build cache: $entry" >&2
                printf '%s\n' "${entry//\\//}"
                return 0
            fi
        done < <(printf '%s\n' "${cached//;/$'\n'}")
    fi

    find_qt_prefix
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

# Print the vswhere generator matching the newest Visual Studio install, e.g.
# "Visual Studio 16 2019". Used as a fallback when Ninja is unavailable.
vs_generator() {
    local vswhere ver major
    for vswhere in \
        "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" \
        "C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"; do
        [[ -f "$vswhere" ]] || continue
        ver="$("$vswhere" -latest -products '*' -property installationVersion 2>/dev/null | awk 'NR == 1 { print }')"
        [[ -n "$ver" ]] || continue
        major="${ver%%.*}"
        case "$major" in
            18) printf 'Visual Studio 18 2026\n'; return 0 ;;
            17) printf 'Visual Studio 17 2022\n'; return 0 ;;
            16) printf 'Visual Studio 16 2019\n'; return 0 ;;
            15) printf 'Visual Studio 15 2017\n'; return 0 ;;
        esac
    done
    return 1
}

# Print the ninja executable: $NINJA, PATH, or the copy shipped with Qt.
find_ninja() {
    local qt_root candidate
    if [[ -n "${NINJA:-}" && -f "${NINJA:-}" ]]; then
        printf '%s\n' "$NINJA"
        return 0
    fi
    if command -v ninja &>/dev/null; then
        command -v ninja
        return 0
    fi
    qt_root="$(dirname "$(dirname "$QT_PREFIX")")"     # .../Qt/<ver>/<compiler> -> .../Qt
    for candidate in "$qt_root/Tools/Ninja/ninja.exe" "C:/Qt/Tools/Ninja/ninja.exe"; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

# ============================================================================
#  Build directory helpers
# ============================================================================

# Print the value of a CMakeCache.txt key ($2) from file $1.
cache_value() {
    local file="$1" key="$2" line
    [[ -f "$file" ]] || return 1
    while IFS= read -r line; do
        case "$line" in
            "$key":*=*) printf '%s\n' "${line#*=}"; return 0 ;;
        esac
    done < "$file"
    return 1
}

# Print the directory CMake was configured from (empty when there is no cache).
cached_source_dir() {
    cache_value "${1:-$BUILD_DIR}/CMakeCache.txt" CMAKE_HOME_DIRECTORY || true
}

# Normalize a path for comparison: forward slashes, lowercase, no trailing
# slash. Windows is case-insensitive and the cache may store a different drive
# letter case than $PWD.
normalize_path() {
    local p="${1//\\//}"
    p="${p%/}"
    printf '%s\n' "${p,,}"
}

# Print the position of $PWD in MSYS form (C:/foo/bar) for cache comparison.
current_source_dir() {
    if [[ "$PWD" =~ ^/([a-zA-Z])/(.*)$ ]]; then
        printf '%s:/%s\n' "${BASH_REMATCH[1]^^}" "${BASH_REMATCH[2]}"
    else
        printf '%s\n' "$PWD"
    fi
}

# A build directory is reusable only when it was configured for THIS source
# directory. After the project folder is moved or renamed, the stale cache makes
# configure fail with a source-mismatch error, so wipe it.
drop_stale_build_dir() {
    local cached
    cached="$(cached_source_dir || true)"
    [[ -n "$cached" ]] || return 0
    if [[ "$(normalize_path "$cached")" != "$(normalize_path "$(current_source_dir)")" ]]; then
        echo "Stale CMake cache in $BUILD_DIR (configured for $cached); removing it." >&2
        rm -rf "$BUILD_DIR"
    fi
}

# ============================================================================
#  Usage
# ============================================================================

usage() {
    # Print the leading comment block (everything above the first code line).
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "${BASH_SOURCE[0]}"
}

# ============================================================================
#  Argument parsing
# ============================================================================

BUILD_DIR="${BUILD_DIR:-}"
JOBS="${JOBS:-}"
TARGET=""
RUN_TESTS=0
RUN_APP=0
DO_CLEAN=0
ACTION="build"
CMAKE_ARGS="${CMAKE_ARGS:-}"
EXTRA_CONFIGURE_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--debug)      CONFIG="Debug" ;;
        -r|--release)    CONFIG="Release" ;;
        -b|--build-dir)  BUILD_DIR="${2:?--build-dir needs a value}"; shift ;;
        -j|--jobs)       JOBS="${2:?--jobs needs a value}"; shift ;;
        --target)        TARGET="${2:?--target needs a value}"; shift ;;
        -t|--test)       RUN_TESTS=1 ;;
        --run)           RUN_APP=1 ;;
        -c|--clean)      DO_CLEAN=1 ;;
        --generator)     GENERATOR="${2:?--generator needs a value}"; shift ;;
        --qt)            QT_PREFIX="${2:?--qt needs a value}"; shift ;;
        --print-qt)      ACTION="print-qt" ;;
        --print-exe)     ACTION="print-exe" ;;
        -h|--help)       usage; exit 0 ;;
        --)              shift; EXTRA_CONFIGURE_ARGS+=( "$@" ); break ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Run 'bash scripts/build.sh --help' for usage." >&2
            exit 2
            ;;
    esac
    shift
done

# Always run from the project root, regardless of where the script is invoked.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

# Default build directories, kept apart per configuration (single-config
# generators cannot hold Debug and Release in one directory).
if [[ -z "$BUILD_DIR" ]]; then
    if [[ "$CONFIG" == "Release" ]]; then
        BUILD_DIR="build-release"
    else
        BUILD_DIR="build"
    fi
fi

[[ $DO_CLEAN -eq 1 ]] && rm -rf "$BUILD_DIR"

drop_stale_build_dir

# --- Qt ---------------------------------------------------------------------
# Resolved first because the Ninja fallback looks inside the Qt installation.
# Nothing is written to stdout here, so --print-qt stays parseable.
QT_PREFIX="$(resolve_qt_prefix || true)"
if [[ "$ACTION" == "print-qt" ]]; then
    if [[ -z "$QT_PREFIX" ]]; then
        echo "Could not locate a Qt installation (looking for bin/windeployqt.exe)." >&2
        exit 1
    fi
    printf '%s\n' "$QT_PREFIX"
    exit 0
fi

# --- Generator --------------------------------------------------------------
CACHED_GENERATOR="$(cache_value "$BUILD_DIR/CMakeCache.txt" CMAKE_GENERATOR || true)"
if [[ -n "$GENERATOR" ]]; then
    GENERATOR_IS_CACHED=0
elif [[ -n "$CACHED_GENERATOR" ]]; then
    GENERATOR="$CACHED_GENERATOR"
    GENERATOR_IS_CACHED=1
else
    GENERATOR_IS_CACHED=0
fi

NINJA_BIN=""
if [[ -z "$GENERATOR" ]]; then
    if NINJA_BIN="$(find_ninja)"; then
        GENERATOR="Ninja"
    elif GENERATOR="$(vs_generator)"; then
        echo "Ninja not found; falling back to the $GENERATOR generator." >&2
    else
        echo "Error: found neither Ninja nor a Visual Studio installation." >&2
        echo "Install Ninja (Qt ships one under <Qt>/Tools/Ninja) or pass --generator." >&2
        exit 1
    fi
elif [[ "$GENERATOR" != Visual\ Studio* ]]; then
    NINJA_BIN="$(find_ninja || true)"
fi

# Multi-config generators (Visual Studio) pick the configuration at build time;
# single-config generators bake it in at configure time.
if [[ "$GENERATOR" == Visual\ Studio* ]]; then
    MULTI_CONFIG=1
else
    MULTI_CONFIG=0
fi

# Where the executable ends up: single-config generators write it into the
# build directory, multi-config generators add a per-configuration subdirectory.
exe_path() {
    local name="${1:-AgentLauncher}"
    if [[ $MULTI_CONFIG -eq 1 ]]; then
        printf '%s/%s/%s.exe\n' "$BUILD_DIR_ABS" "$CONFIG" "$name"
    else
        printf '%s/%s.exe\n' "$BUILD_DIR_ABS" "$name"
    fi
}
BUILD_DIR_ABS="$(absolute_win_path "$BUILD_DIR")"
EXE="$(exe_path)"

if [[ "$ACTION" == "print-exe" ]]; then
    printf '%s\n' "$EXE"
    exit 0
fi

if [[ -z "$QT_PREFIX" ]]; then
    echo "Error: could not locate a Qt installation (looking for bin/windeployqt.exe)." >&2
    echo "Set QT_PREFIX or pass --qt, e.g. --qt C:/Qt/6.7.3/msvc2019_64" >&2
    exit 1
fi

echo "Qt:     $QT_PREFIX"
echo "Config: $CONFIG"
echo "Build:  $BUILD_DIR (generator: $GENERATOR)"

# --- Cached configuration checks --------------------------------------------
if [[ -n "$CACHED_GENERATOR" && "$GENERATOR_IS_CACHED" -eq 0 && "$GENERATOR" != "$CACHED_GENERATOR" ]]; then
    echo "Error: $BUILD_DIR is configured with generator '$CACHED_GENERATOR', but '$GENERATOR' was requested." >&2
    echo "Use --clean to wipe the directory, or pick another --build-dir." >&2
    exit 1
fi
if [[ $MULTI_CONFIG -eq 0 ]]; then
    CACHED_CONFIG="$(cache_value "$BUILD_DIR/CMakeCache.txt" CMAKE_BUILD_TYPE || true)"
    if [[ -n "$CACHED_CONFIG" && "$CACHED_CONFIG" != "$CONFIG" ]]; then
        echo "Switching $BUILD_DIR from $CACHED_CONFIG to $CONFIG; reconfiguring from scratch." >&2
        rm -rf "$BUILD_DIR"
    fi
fi

# --- Configure + build command lines ----------------------------------------
# Everything is decided here in bash; the generated .bat only runs these lines.
CONFIGURE_ARGS=( -G "$GENERATOR" -B "$BUILD_DIR_ABS" )
CONFIGURE_ARGS+=( "-DCMAKE_PREFIX_PATH=$QT_PREFIX" )
# compile_commands.json lets editors and agents resolve headers and symbols.
CONFIGURE_ARGS+=( -DCMAKE_EXPORT_COMPILE_COMMANDS=ON )
[[ $MULTI_CONFIG -eq 0 ]] && CONFIGURE_ARGS+=( "-DCMAKE_BUILD_TYPE=$CONFIG" )
[[ -n "$NINJA_BIN" ]] && CONFIGURE_ARGS+=( "-DCMAKE_MAKE_PROGRAM=$(to_windows_path "$NINJA_BIN")" )
if [[ -n "${CMAKE_ARGS// /}" ]]; then
    read -r -a _extra <<<"$CMAKE_ARGS"
    CONFIGURE_ARGS+=( "${_extra[@]}" )
fi
if [[ ${#EXTRA_CONFIGURE_ARGS[@]} -gt 0 ]]; then
    CONFIGURE_ARGS+=( "${EXTRA_CONFIGURE_ARGS[@]}" )
fi

BUILD_ARGS=( --build "$BUILD_DIR_ABS" --config "$CONFIG" )
[[ -n "$JOBS" ]] && BUILD_ARGS+=( --parallel "$JOBS" )
[[ -n "$TARGET" ]] && BUILD_ARGS+=( --target "$TARGET" )

# ============================================================================
#  Build
# ============================================================================

# Join arguments into a single quoted command line for cmd.exe.
join_quoted() {
    local out="$1" arg; shift
    for arg in "$@"; do
        out+=" \"$arg\""
    done
    printf '%s\n' "$out"
}

# Run the CMake commands through a .bat that loads the MSVC environment, then
# runs configure and build in sequence. The command lines are baked into the
# file: passing them to `cmd /c` from Git Bash mangles quoting.
run_msvc_build() {
    local bat="$BUILD_DIR/.build-agentlauncher.bat"
    local vcvars_win cmake_dir ninja_dir configure_cmd build_cmd
    mkdir -p "$BUILD_DIR"

    vcvars_win="$(to_win_backslash_path "$VCVARS")"
    cmake_dir="$(to_win_backslash_path "$(dirname "$(command -v cmake)")")"
    [[ -n "$NINJA_BIN" ]] && ninja_dir="$(to_win_backslash_path "$(dirname "$NINJA_BIN")")"
    configure_cmd="$(join_quoted cmake "${CONFIGURE_ARGS[@]}")"
    build_cmd="$(join_quoted cmake "${BUILD_ARGS[@]}")"

    {
        printf '@echo off\r\n'
        printf 'setlocal\r\n'
        printf 'call "%s" >nul 2>nul\r\n' "$vcvars_win"
        printf 'if errorlevel 1 (\r\n'
        printf '    echo [build.sh] ERROR: could not load the MSVC environment. 1>&2\r\n'
        printf '    exit /b 1\r\n'
        printf ')\r\n'
        printf 'cd /d "%s" || exit /b 1\r\n' "$(to_win_backslash_path "$PWD")"
        printf 'set "PATH=%s;%%PATH%%"\r\n' "$cmake_dir"
        [[ -n "$NINJA_BIN" ]] && printf 'set "PATH=%s;%%PATH%%"\r\n' "$ninja_dir"
        printf '%s\r\n' "$configure_cmd"
        printf 'if errorlevel 1 exit /b 1\r\n'
        printf '%s\r\n' "$build_cmd"
        printf 'if errorlevel 1 exit /b 1\r\n'
        printf 'exit /b 0\r\n'
    } > "$bat"

    cmd //c "$(to_win_backslash_path "$bat")"
}

# Steps shown to the user, numbered on the fly.
STEP=0
step() {
    STEP=$((STEP + 1))
    echo ""
    echo "=== $1 ==="
}

build_with_msvc() {
    step "Configure + build (MSVC via $VCVARS)"
    run_msvc_build
}

build_with_cmake() {
    step "Configure"
    cmake "${CONFIGURE_ARGS[@]}"
    step "Build"
    cmake "${BUILD_ARGS[@]}"
}

if [[ "$GENERATOR" == Visual\ Studio* ]]; then
    # MSBuild locates the toolchain itself, so no vcvars environment is needed.
    build_with_cmake
else
    VCVARS="$(find_vcvars || true)"
    if [[ -z "$VCVARS" ]]; then
        echo "Error: the $GENERATOR generator needs the MSVC environment, but no" >&2
        echo "vcvars64.bat was found. Install Visual Studio C++ tools, set VCVARS," >&2
        echo "or build with --generator \"Visual Studio 16 2019\"." >&2
        exit 1
    fi
    build_with_msvc
fi

# --- Tests ------------------------------------------------------------------
if [[ $RUN_TESTS -eq 1 ]]; then
    step "Run tests"
    if ! command -v ctest &>/dev/null; then
        echo "Warning: ctest not found; skipping tests." >&2
    else
        # Qt's bin directory holds the runtime DLLs the test executable needs.
        if ! ( cd "$BUILD_DIR" && PATH="$QT_PREFIX/bin:$PATH" ctest -C "$CONFIG" --output-on-failure ); then
            echo "" >&2
            echo "Tests FAILED." >&2
            exit 1
        fi
        echo "Tests passed."
    fi
fi

# --- Launch -----------------------------------------------------------------
if [[ $RUN_APP -eq 1 ]]; then
    step "Launch"
    # `start` detaches, so the script returns while the app keeps running.
    cmd //c start "" "$(to_win_backslash_path "$EXE")"
    echo "Launched $EXE"
fi

echo ""
echo "=== Build succeeded ==="
echo "Configuration: $CONFIG"
echo "Executable:    $EXE"
if [[ $RUN_TESTS -eq 0 ]]; then
    echo "Tests:         not run (add --test)"
fi
