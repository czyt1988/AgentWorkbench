#!/usr/bin/env bash
# AgentWorkbench translation sync script.
# Usage: bash scripts/update-ts.sh
#
# Runs lupdate over every source listed in cmake/AwbTranslations.cmake
# (AWB_TS_SOURCES) and rewrites translations/agentworkbench_zh_CN.ts in the
# source tree. This is the ONLY thing that touches the .ts — the default
# build only compiles .qm (lrelease), so the worktree stays clean.
#
# Run this BEFORE committing a change that adds or modifies a tr()/qsTr()
# source string, then commit the .ts together with the code change, and fill
# in any new untranslated entries (a <translation type="unfinished"> block)
# before releasing.
#
# Prerequisites: the build directory must already be configured (the script
# drives the update_ts target defined in the root CMakeLists.txt, which
# resolves the Qt 5/Qt 6 lupdate binary and the source list). If build/
# does not exist yet, run bash scripts/build.sh once first.
#
# Exit codes: non-zero when lupdate fails (the .ts may have been partially
# rewritten — check git diff before retrying).

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"

cd "$(dirname "$0")/.."

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "error: ${BUILD_DIR} is not a configured build directory." >&2
    echo "       Run bash scripts/build.sh once first." >&2
    exit 1
fi

echo "Syncing translations/agentworkbench_zh_CN.ts from source ..."
cmake --build "${BUILD_DIR}" --target update_ts

# lupdate exits 0 even when it reports "N unfinished messages left" — that is
# expected; the list of unfinished entries below tells the translator what to
# fill in before the next release.
if git diff --quiet translations/ ; then
    echo "translations/ unchanged — the .ts is already in sync."
else
    echo "translations/ updated. Fill in any unfinished entries, then commit"
    echo "the .ts together with the string change:"
    git status --short translations/
fi
