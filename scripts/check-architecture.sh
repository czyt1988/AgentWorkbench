#!/usr/bin/env bash
# Architecture checks (specs/01-architecture.md §5.3). Pure shell + grep;
# exit code 0 = clean, non-zero = a rule was broken. Wired into ctest as the
# `check_architecture` test, so a violating build fails instead of relying
# on people remembering the rules.
#
# Checks:
#   1. Reverse/sideways includes: domain modules must not include shell/,
#      workbench/ or each other.
#   2. Visual literals: QML must not hardcode #rrggbb / #rgb colors
#      ("transparent" and theme.* are allowed). Matched case-sensitively in
#      lowercase form — the documented hex notation; uppercase sequences in
#      user-facing documentation strings are not color usages.
#   3. i18n: tr()/qsTr() source strings must be ASCII.
#   4. core purity: src/core/ and src/theme/ must not pull in Qt Quick or
#      WebEngine.

set -u

cd "$(dirname "$0")/.."
status=0
fail() {
    echo "check-architecture: FAIL: $1" >&2
    status=1
}

# --- 1. Dependency direction -------------------------------------------------
# Domain modules: which sibling directories each one must not mention.
for domain in agents skills web; do
    others=""
    case "$domain" in
        agents) others="skills web" ;;
        skills) others="agents web" ;;
        web)    others="agents skills" ;;
    esac
    # shell/ and workbench/ are also forbidden (domain modules never depend
    # on the UI framework or the application layer).
    forbidden="shell/ workbench/"
    for other in $others; do
        forbidden="$forbidden $other/"
    done

    while IFS= read -r file; do
        for inc in $forbidden; do
            if grep -nE "^[[:space:]]*#include[[:space:]]+\"[^\"]*${inc}" "$file" >/dev/null 2>&1; then
            hit="$(grep -nE "^[[:space:]]*#include[[:space:]]+\"[^\"]*${inc}" "$file")"
            fail "src/$domain: forbidden include of ${inc} in $file: $hit"
        fi
        done
    done < <(find "src/$domain" -name '*.h' -o -name '*.cpp' 2>/dev/null)
done

# --- 2. Visual literals in QML ----------------------------------------------
# Legacy qml/ (until S4 retires it) and every module's qml/ directory.
while IFS= read -r file; do
    if grep -nE '#[0-9a-f]{6}|#[0-9a-f]{3}([^0-9a-f]|$)' "$file" | grep -vE '#RRGGBB|#RGB' > /dev/null 2>&1; then
        hits="$(grep -nE '#[0-9a-f]{6}|#[0-9a-f]{3}([^0-9a-f]|$)' "$file" | grep -vE '#RRGGBB|#RGB')"
        fail "hardcoded color literal in $file:"$'\n'"$hits"
    fi
done < <(find qml src -name '*.qml' 2>/dev/null)

# --- 3. English-only source strings -----------------------------------------
while IFS= read -r file; do
    # A tr()/qsTr() call on one line containing any non-ASCII byte.
    if grep -nE 'qs?Tr\("[^"]*"' "$file" | grep -qP '[^\x00-\x7F]'; then
        hits="$(grep -nE 'qs?Tr\("[^"]*"' "$file" | grep -P '[^\x00-\x7F]')"
        fail "non-ASCII source string in tr()/qsTr() in $file:"$'\n'"$hits"
    fi
done < <(find src qml -name '*.cpp' -o -name '*.h' -o -name '*.qml' 2>/dev/null)

# --- 4. core purity ----------------------------------------------------------
for dir in src/core src/theme; do
    while IFS= read -r file; do
        if grep -nE 'QtQuick|Qt6::Quick|QtWebEngine' "$file" >/dev/null 2>&1; then
            hits="$(grep -nE 'QtQuick|Qt6::Quick|QtWebEngine' "$file")"
            fail "UI framework reference in $file (core/theme must stay UI-free):"$'\n'"$hits"
        fi
    done < <(find "$dir" -name '*.h' -o -name '*.cpp' 2>/dev/null)
done

if [[ $status -eq 0 ]]; then
    echo "check-architecture: OK"
fi
exit $status
