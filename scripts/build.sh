#!/usr/bin/env bash
# AgentWorkbench build script — configure + compile in one command.
# Usage: bash scripts/build.sh [options]
#
# Prerequisites: Qt 6.5+ or Qt 5.15 LTS (5.15.16+ verified; the msvc build is
# recommended) and, for MSVC builds driven from Git Bash, Visual Studio with
# the C++ tools. Both are auto-detected.
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
#       --target NAME      Build one target only (AgentWorkbench, tst_core,
#                          tst_agentcatalog, tst_theme, tst_shell, tst_web,
#                          tst_skillcatalog)
#   -t, --test             Run the unit tests (ctest) after building
#                          (configures with -DBUILD_TESTING=ON)
#       --no-tests         Configure without the test targets
#                          (-DBUILD_TESTING=OFF), e.g. for release packaging
#       --run              Launch the application after building (detached)
#   -c, --clean            Delete the build directory first (full rebuild)
#       --generator NAME   Force a CMake generator (default: keep whatever the
#                          build directory was configured with, else Ninja)
#       --qt PATH          Qt prefix override (same as QT_PREFIX=...)
#       --print-qt         Print the resolved Qt prefix and exit (detection only)
#       --print-exe        Print the built executable path and exit
#   -h, --help             Show this help
#
# Qt is auto-detected: an explicit QT_PREFIX first, then the prefix the build
# directory was configured with, then the common install locations on every
# drive, then a qmake/qtpaths on PATH, then a time-capped scan of every drive.
# When nothing usable is found, the script lists what it saw -- including the
# installs it had to reject -- and, on a terminal, asks for the prefix instead
# of failing with a bare error.
#
# Environment overrides: QT_PREFIX, VCVARS, GENERATOR, BUILD_DIR, JOBS,
#                        CMAKE_ARGS (extra configure arguments),
#                        AWB_QT_DEEP_SEARCH=0 (skip the drive scan),
#                        QT_DEEP_TIMEOUT=<seconds per drive, default 20>
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
# auto-detect the newest usable msvc install on this machine (Qt 6 preferred,
# 5.15 LTS accepted).
QT_PREFIX="${QT_PREFIX:-}"

# Visual Studio vcvars64.bat path. Leave empty to auto-detect via vswhere.
VCVARS="${VCVARS:-}"

# CMake generator. Leave empty to keep the generator the build directory was
# configured with, or to prefer Ninja for a fresh build directory.
GENERATOR="${GENERATOR:-}"

# Minimum Qt version accepted by the build (see CMakeLists.txt). Qt 6.5+ is
# preferred when present; a 5.15.16+ LTS install is accepted as the fallback
# (older 5.15.x patch levels lack the WebEngine backports the embedded Web
# surface needs).
QT_MIN_VERSION="5.15.16"

# ============================================================================
#  Qt / MSVC / Ninja detection
# ============================================================================

# Every Qt prefix that was found but rejected, keyed by normalized path, mapped
# to the reason it cannot build this project. Filled by
# consider_qt_candidate() and read by report_no_qt(), so the search must run in
# this shell -- inside a $(...) subshell these notes would be discarded.
declare -A QT_REJECTED=()
# Every location the search looked at, for the same report; reset per search.
QT_SEARCHED=()
# Best usable candidate so far, its version, and the priority of its compiler
# (0 = msvc, 1 = anything else).
QT_BEST=""
QT_BEST_VER=""
QT_BEST_PRIO=99
# Why the last candidate was rejected; empty after an accepted one.
QT_LAST_REASON=""
# Result of resolve_qt_prefix(); a global rather than stdout for the same reason.
RESOLVED_QT=""

# True when $1 is an existing Qt prefix containing bin/windeployqt.exe.
qt_prefix_is_valid() {
    local p="${1//\\//}"
    [[ -n "$p" && -f "${p%/}/bin/windeployqt.exe" ]]
}

# True when $1 is a Qt development install, i.e. ships the CMake package that
# the find_package in CMakeLists.txt consumes (Qt 6 or the 5.15 fallback).
qt_is_qt_dev() {
    local p="${1//\\//}"
    [[ -f "${p%/}/lib/cmake/Qt6/Qt6Config.cmake" \
       || -f "${p%/}/lib/cmake/Qt5/Qt5Config.cmake" ]]
}

# Print the Qt version of prefix $1: from its CMake version file when there is
# one, else from the <root>/<version>/<compiler> layout. "0" means the version
# could not be determined -- no Qt6 CMake package and no version-shaped parent
# directory.
qt_version_of() {
    local p="${1%/}" f v
    f="$p/lib/cmake/Qt6/Qt6ConfigVersion.cmake"
    if [[ -f "$f" ]]; then
        v="$(sed -nE 's/^[[:space:]]*set\(PACKAGE_VERSION[[:space:]]+"([^"]+)".*/\1/p' "$f" | head -n1)"
        if [[ -n "$v" ]]; then
            printf '%s\n' "$v"
            return 0
        fi
    fi
    v="$(basename "$(dirname "$p")")"
    if [[ "$v" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then
        printf '%s\n' "$v"
    else
        printf '0\n'
    fi
}

# Succeed when version $1 is >= $2 (version-sort comparison).
version_at_least() {
    [[ -n "${1:-}" ]] || return 1
    local newest
    newest="$(printf '%s\n%s\n' "$2" "$1" | sort -V | tail -n 1)"
    [[ "$newest" == "$1" ]]
}

# Print why the Qt install at $1 cannot build this project; print nothing when
# it can.
qt_rejection() {
    local p="${1%/}" ver
    if ! qt_prefix_is_valid "$p"; then
        printf '%s\n' "no bin/windeployqt.exe"
        return 0
    fi
    ver="$(qt_version_of "$p")"
    if [[ "$ver" == "0" ]]; then
        if qt_is_qt_dev "$p"; then
            return 0
        fi
        printf '%s\n' "not a Qt development install (no lib/cmake/Qt5 or Qt6)"
        return 0
    fi
    if version_at_least "$ver" "$QT_MIN_VERSION"; then
        return 0
    fi
    printf 'Qt %s is older than the required %s\n' "$ver" "$QT_MIN_VERSION"
}

# Print one candidate Qt prefix per line for every Qt installation below $1:
# <root>/<compiler>, <root>/<version>/<compiler> and
# <root>/<group>/<version>/<compiler>. The last is what the Qt online installer
# lays out for Qt 5 (D:/Qt/Qt5.15.16/5.15.16/msvc2019_64) and used to be
# reachable only through the whole-drive find -- see qt_candidates_deep.
qt_candidates_in() {
    local root="${1%/}" d
    [[ -n "$root" && -d "$root" ]] || return 0
    for d in "$root" "$root"/* "$root"/*/* "$root"/*/*/*; do
        [[ -f "$d/bin/windeployqt.exe" ]] || continue
        printf '%s\n' "$d"
    done
}

# Print Qt prefixes reachable through PATH. A qmake/qtpaths shim names the
# installation this machine builds against outside this script (a Qt Creator
# kit, an MSVC developer prompt, an aqtinstall shim).
qt_candidates_from_path() {
    local tool prefix
    for tool in qmake6 qmake qtpaths6 qtpaths; do
        command -v "$tool" &>/dev/null || continue
        prefix="$("$tool" -query QT_INSTALL_PREFIX 2>/dev/null | tr -d '\r')"
        if [[ -n "$prefix" ]]; then
            printf '%s\n' "${prefix//\\//}"
        fi
    done
}

# Last resort: every windeployqt.exe below one drive root. Capped in depth and
# in wall-clock time, because walking a whole drive takes minutes -- long enough
# to read as a hang -- and because Program Files is covered directly by the
# common roots, so it can be pruned here.
qt_candidates_deep() {
    local drive="$1" secs="${QT_DEEP_TIMEOUT:-20}" finder=( find )
    if command -v timeout &>/dev/null; then
        finder=( timeout "$secs" find )
    fi
    "${finder[@]}" "$drive" -maxdepth 6 \
        \( -type d \( -name 'Program Files' -o -name 'Program Files (x86)' \
           -o -name Windows -o -name ProgramData -o -name '$Recycle.Bin' \
           -o -name 'System Volume Information' -o -name Recovery \) -prune \) -o \
        -type f -name 'windeployqt.exe' -path '*/bin/*' -print 2>/dev/null \
    | sed 's#/bin/windeployqt\.exe$##' || true
}

# Score one candidate prefix: accept it when it can build this project and beats
# everything seen so far (msvc over mingw first, then the newest version).
# Unusable candidates are remembered so report_no_qt() can name them.
consider_qt_candidate() {
    local line ver prio reason
    line="$(to_windows_path "${1%/}")"
    line="${line//\\//}"
    [[ -n "$line" ]] || return 1

    QT_LAST_REASON=""
    reason="$(qt_rejection "$line")"
    if [[ -n "$reason" ]]; then
        QT_REJECTED["$line"]="$reason"
        QT_LAST_REASON="$reason"
        return 1
    fi

    ver="$(qt_version_of "$line")"
    case "$(basename "$line")" in
        msvc*) prio=0 ;;
        *)     prio=1 ;;
    esac
    if [[ -n "$QT_BEST" ]]; then
        if (( prio > QT_BEST_PRIO )); then
            return 1
        fi
        if (( prio == QT_BEST_PRIO )) && ! version_at_least "$ver" "$QT_BEST_VER"; then
            return 1
        fi
    fi
    QT_BEST="$line"
    QT_BEST_VER="$ver"
    QT_BEST_PRIO="$prio"
    return 0
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

# Convert a Windows path (D:/foo or D:\foo) to the Git-Bash/MSYS form (/d/foo).
# Required for PATH prepends: a drive-letter path inside the POSIX PATH list
# gets split at the colon in "D:" when MSYS converts the list for a native
# process, leaving garbage entries (D, <git-root>/Qt/...) that resolve nothing.
to_msys_path() {
    local p="$1" drive rest
    if [[ "$p" =~ ^([a-zA-Z]):[/\\](.*)$ ]]; then
        drive="$(printf '%s' "${BASH_REMATCH[1]}" | tr '[:upper:]' '[:lower:]')"
        rest="${BASH_REMATCH[2]//\\//}"
        printf '/%s/%s\n' "$drive" "$rest"
    else
        printf '%s\n' "$p"
    fi
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
#   1. explicit $QT_PREFIX / --qt (usable wins outright, unusable is fatal --
#      the project cannot be built against it either way, so say so now)
#   2. the common install roots: every existing drive's Qt folders plus the
#      per-user locations the Qt online installer defaults to
#   3. a qmake/qtpaths on PATH
#   4. a time-capped scan of every mounted drive
# Sets QT_BEST; on failure QT_REJECTED/QT_SEARCHED describe what was found.
find_qt_prefix() {
    local root candidate letter up
    local roots=()

    QT_SEARCHED=( "QT_PREFIX" "the prefix in $BUILD_DIR/CMakeCache.txt" )

    if [[ -n "${QT_PREFIX:-}" ]]; then
        if qt_prefix_is_valid "$QT_PREFIX"; then
            if consider_qt_candidate "$QT_PREFIX"; then
                echo "Using Qt from QT_PREFIX: $QT_BEST" >&2
                return 0
            fi
            # Explicit and unusable: stop here rather than quietly building
            # against some other Qt the user did not ask for. report_no_qt()
            # names the path and the reason.
            return 1
        fi
        echo "Warning: QT_PREFIX '$QT_PREFIX' has no bin/windeployqt.exe; trying auto-detection." >&2
    fi

    # Common roots: the per-user paths are the Qt online installer's default,
    # the per-drive ones cover an install folder chosen by hand.
    if [[ -n "${USERPROFILE:-}" ]]; then
        up="${USERPROFILE//\\//}"
        roots+=( "${up%/}/Qt" )
    fi
    if [[ -n "${LOCALAPPDATA:-}" ]]; then
        up="${LOCALAPPDATA//\\//}"; up="${up%/}"
        roots+=( "$up/Programs/Qt" "$up/Qt" )
    fi
    for letter in {c..z}; do
        [[ -d "/$letter" ]] || continue
        roots+=( "/$letter/Qt" "/$letter/Program Files/Qt" "/$letter/Program Files (x86)/Qt" )
    done

    for root in "${roots[@]}"; do
        [[ -d "$root" ]] || continue
        QT_SEARCHED+=( "$(to_windows_path "$root")" )
        while IFS= read -r candidate; do
            consider_qt_candidate "$candidate" || true
        done < <(qt_candidates_in "$root")
    done
    if [[ -n "$QT_BEST" ]]; then
        echo "Found Qt: $QT_BEST" >&2
        return 0
    fi

    QT_SEARCHED+=( "qmake/qtpaths on PATH" )
    while IFS= read -r candidate; do
        consider_qt_candidate "$candidate" || true
    done < <(qt_candidates_from_path)
    if [[ -n "$QT_BEST" ]]; then
        echo "Found Qt via qmake on PATH: $QT_BEST" >&2
        return 0
    fi

    if [[ "${AWB_QT_DEEP_SEARCH:-1}" == "0" ]]; then
        QT_SEARCHED+=( "every drive (scan disabled by AWB_QT_DEEP_SEARCH=0)" )
        return 1
    fi

    # Announced before it starts: this is the slow step, and silence here is
    # exactly what makes the script look hung.
    echo "Qt not found in the common locations; scanning the drives for windeployqt.exe" >&2
    echo "(up to ${QT_DEEP_TIMEOUT:-20}s per drive; set QT_PREFIX to skip, AWB_QT_DEEP_SEARCH=0 to disable)" >&2
    for letter in {c..z}; do
        [[ -d "/$letter" ]] || continue
        echo "  scanning ${letter^^}:/ ..." >&2
        QT_SEARCHED+=( "${letter^^}:/ (windeployqt.exe scan)" )
        while IFS= read -r candidate; do
            consider_qt_candidate "$candidate" || true
        done < <(qt_candidates_deep "/$letter")
        if [[ -n "$QT_BEST" ]]; then
            echo "Found Qt on ${letter^^}:/ : $QT_BEST" >&2
            return 0
        fi
    done

    return 1
}

# Resolve the Qt prefix this build would use: explicit override, else the one
# stored in the build directory, else auto-detection. Sets RESOLVED_QT (empty
# when nothing was found) rather than printing it: the search has to run in this
# shell so that report_no_qt() can see what it rejected.
resolve_qt_prefix() {
    local cached entry

    RESOLVED_QT=""

    if [[ -n "${QT_PREFIX:-}" ]]; then
        find_qt_prefix || return $?
        RESOLVED_QT="$QT_BEST"
        return 0
    fi

    cached="$(cache_value "$BUILD_DIR/CMakeCache.txt" CMAKE_PREFIX_PATH || true)"
    if [[ -n "$cached" ]]; then
        while IFS= read -r entry; do
            [[ -n "$entry" ]] || continue
            if qt_prefix_is_valid "$entry"; then
                if consider_qt_candidate "$entry"; then
                    echo "Using Qt from the existing build cache: $QT_BEST" >&2
                    RESOLVED_QT="$QT_BEST"
                    return 0
                fi
                echo "Warning: $BUILD_DIR was configured with $entry, which cannot" >&2
                echo "         build this project: $QT_LAST_REASON" >&2
            fi
        done < <(printf '%s\n' "${cached//;/$'\n'}")
    fi

    find_qt_prefix || return $?
    RESOLVED_QT="$QT_BEST"
    return 0
}

# Explain a failed search: where it looked and which Qt installs it had to
# reject. Goes to stderr, so --print-qt's stdout stays a bare path.
report_no_qt() {
    local p

    echo "" >&2
    echo "Error: no usable Qt installation found. AgentWorkbench needs Qt $QT_MIN_VERSION or newer." >&2
    if [[ ${#QT_REJECTED[@]} -eq 0 ]]; then
        echo "No Qt installation was found at all." >&2
    else
        echo "Found, but unusable for this project:" >&2
        while IFS= read -r p; do
            [[ -n "$p" ]] || continue
            echo "  $p" >&2
            echo "      ${QT_REJECTED[$p]}" >&2
        done < <(printf '%s\n' "${!QT_REJECTED[@]}" | sort)
    fi
    echo "Looked at:" >&2
    while IFS= read -r p; do
        [[ -n "$p" ]] || continue
        echo "  $p" >&2
    done < <(printf '%s\n' "${QT_SEARCHED[@]}")
    echo "" >&2
    echo "Install Qt $QT_MIN_VERSION+ (the msvc2019_64 package of the Qt online" >&2
    echo "installer works) and point this script at it:" >&2
    echo "  bash scripts/build.sh --qt C:/Qt/6.7.3/msvc2019_64" >&2
    echo "  QT_PREFIX=C:/Qt/6.7.3/msvc2019_64 bash scripts/build.sh" >&2
    echo "A 5.15.16 LTS msvc2019_64 install works as well:" >&2
    echo "  bash scripts/build.sh --qt D:/Qt/Qt5.15.16/5.15.16/msvc2019_64" >&2
}

# Ask the user which Qt to use, after auto-detection failed. Reads the terminal
# only: a non-interactive run (agent, CI, piped stdin) has to fail with the
# error above instead of blocking on a read nobody can answer. An answer that
# cannot build this project is rejected with the same reason the search used.
prompt_for_qt() {
    local reply reason attempt

    # stdin when it is the terminal itself; otherwise the console behind it, so
    # that a script piping stdin into build.sh still gets a prompt. When neither
    # exists (agent, CI, service) there is nobody to answer, and the error text
    # above is the whole answer.
    if [[ -t 0 ]]; then
        exec 3<&0
    elif [[ -r /dev/tty ]] && { exec 3< /dev/tty; } 2>/dev/null; then
        :
    else
        return 1
    fi

    for attempt in 1 2 3; do
        printf '%s' "Qt prefix (e.g. C:/Qt/6.7.3/msvc2019_64), Enter to abort: " >&2
        if ! IFS= read -r reply <&3; then
            exec 3<&-
            return 1
        fi
        # Trim the answer, then undo Explorer's "Copy as path" quoting; a pasted
        # Windows path needs its backslashes turned into slashes.
        reply="${reply#"${reply%%[![:space:]]*}"}"
        reply="${reply%"${reply##*[![:space:]]}"}"
        reply="${reply%\"}"
        reply="${reply#\"}"
        reply="${reply//\\//}"
        if [[ -z "$reply" ]]; then
            exec 3<&-
            return 1
        fi
        reply="$(to_windows_path "$reply")"
        reason="$(qt_rejection "$reply")"
        if [[ -n "$reason" ]]; then
            echo "  $reply cannot build this project: $reason" >&2
            continue
        fi
        QT_BEST="${reply%/}"
        exec 3<&-
        echo "Using Qt: $QT_BEST" >&2
        return 0
    done

    exec 3<&-
    echo "No usable Qt prefix given; giving up." >&2
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
# BUILD_TESTING value to configure with. Empty leaves the option to the CMake
# default / existing cache; --test and --no-tests set it explicitly so that a
# build directory keeps working after the other flag was used on it.
TESTS=""
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
        -t|--test)       RUN_TESTS=1; TESTS="ON" ;;
        --no-tests)      TESTS="OFF" ;;
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

if [[ "$TESTS" == "OFF" && $RUN_TESTS -eq 1 ]]; then
    echo "Error: --test and --no-tests are mutually exclusive." >&2
    exit 2
fi

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
# resolve_qt_prefix() sets a variable rather than printing, so that the notes
# behind report_no_qt() survive, and --print-qt prints only the bare path.
resolve_qt_prefix || true
QT_PREFIX="$RESOLVED_QT"

if [[ "$ACTION" == "print-qt" ]]; then
    if [[ -z "$QT_PREFIX" ]]; then
        report_no_qt
        exit 1
    fi
    printf '%s\n' "$QT_PREFIX"
    exit 0
fi

# No Qt found for a build: say what was there and why it does not work, then let
# the user name one by hand (on a terminal; otherwise the error above is the
# answer). --print-exe does not need Qt and is unaffected.
if [[ "$ACTION" == "build" && -z "$QT_PREFIX" ]]; then
    report_no_qt
    if prompt_for_qt; then
        QT_PREFIX="$QT_BEST"
    else
        exit 1
    fi
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
    local name="${1:-AgentWorkbench}"
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
    report_no_qt
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
# Placed before the user-supplied arguments so an explicit
# `-- -DBUILD_TESTING=...` still wins over --test / --no-tests.
[[ -n "$TESTS" ]] && CONFIGURE_ARGS+=( "-DBUILD_TESTING=$TESTS" )
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
    local bat="$BUILD_DIR/.build-agentworkbench.bat"
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
        # It must be prepended in MSYS form — see to_msys_path().
        if ! ( cd "$BUILD_DIR" && PATH="$(to_msys_path "$QT_PREFIX")/bin:$PATH" ctest -C "$CONFIG" --output-on-failure ); then
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
    # Qt's bin is prepended (MSYS form) so the app finds its runtime DLLs even
    # when the Qt used for the build is not on the system PATH.
    PATH="$(to_msys_path "$QT_PREFIX")/bin:$PATH" \
        cmd //c start "" "$(to_win_backslash_path "$EXE")"
    echo "Launched $EXE"
fi

echo ""
echo "=== Build succeeded ==="
echo "Configuration: $CONFIG"
echo "Executable:    $EXE"
if [[ "$TESTS" == "OFF" ]]; then
    echo "Tests:         not built (--no-tests)"
elif [[ $RUN_TESTS -eq 0 ]]; then
    echo "Tests:         not run (add --test)"
fi
