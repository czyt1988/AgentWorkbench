#!/usr/bin/env bash
# Architecture checks. Pure shell + grep;
# exit code 0 = clean, non-zero = a rule was broken. Wired into ctest as the
# `check_architecture` test, so a violating build fails instead of relying
# on people remembering the rules.
#
# Checks:
#   1. Reverse/sideways includes: domain modules must not include shell/,
#      workbench/ or each other.
#   2. Visual literals: QML must not hardcode #rrggbb / #rgb colors or
#      Qt.rgba(<number>, …) built from numeric literals ("transparent",
#      theme.* and Qt.rgba(expr, …) function calls are allowed). Hex is
#      matched case-sensitively in lowercase form — the documented notation;
#      uppercase sequences in user-facing documentation strings are not
#      color usages.
#   3. i18n: tr()/qsTr() source strings must be ASCII.
#   4. core purity: src/core/ and src/theme/ must not pull in Qt Quick,
#      QML or WebEngine (QQuick*, QQml*, QtQuick, Qt6::Quick, QtWebEngine).
#   5. QML → C++ callability: every singleton.<method>( call in QML must be
#      Q_INVOKABLE (or a slot/signal) in the declaring header, and every
#      singleton.<prop> = write needs a Q_PROPERTY WRITE. A bare method or
#      property accessor is NOT in the meta-object method table — QML throws
#      "…is not a function" / "read-only property" at click time, which
#      page-load smoke never exercises (this exact gap hid three broken
#      sidebar/settings interactions until a manual run found them).

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
    # Qt.rgba built from numeric literals (137/255, 0.22, …) — a color
    # literal in disguise; use theme.alpha(color, a) instead. Qt.rgba with
    # an expression first (tintRed(x), theme.accent, …) is allowed.
    if grep -nE 'Qt\.rgba\([[:space:]]*[0-9]' "$file" > /dev/null 2>&1; then
        hits="$(grep -nE 'Qt\.rgba\([[:space:]]*[0-9]' "$file")"
        fail "numeric Qt.rgba() color literal in $file (use theme.alpha):"$'\n'"$hits"
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
# QQuick*/QQml* are included as <QQuickItem> etc. — "QtQuick" alone misses
# them, which is how a QML-engine include slipped through review.
for dir in src/core src/theme; do
    while IFS= read -r file; do
        if grep -nE 'QtQuick|QQuick|QQml|Qt6::Quick|QtWebEngine' "$file" >/dev/null 2>&1; then
            hits="$(grep -nE 'QtQuick|QQuick|QQml|Qt6::Quick|QtWebEngine' "$file")"
            fail "UI framework reference in $file (core/theme must stay UI-free):"$'\n'"$hits"
        fi
    done < <(find "$dir" -name '*.h' -o -name '*.cpp' 2>/dev/null)
done

# --- 5. QML → C++ 可调用性 --------------------------------------------------
# QML can only call Q_INVOKABLE/slot methods on a C++ object: a bare method
# (or a Q_PROPERTY WRITE used as a method) is not in the meta-object method
# table and throws "…is not a function" at click time. Page-load smoke never
# exercises clicks, so this rule is the gate for that whole bug class.
#
# Every `<alias>.<method>(` in QML must resolve to Q_INVOKABLE <method>( in
# the header that declares the alias's type; `<alias>.<prop> =` writes must
# have a Q_PROPERTY WRITE. QML-defined ids (page, card, flyout, model …)
# are intentionally NOT in the alias list — they resolve to QML functions.
alias_header() {
    case "$1" in
        theme) echo "src/theme/Theme.h" ;;
        nav) echo "src/shell/NavigationModel.h" ;;
        shell) echo "src/shell/ShellController.h" ;;
        ui) echo "src/shell/UiServices.h" ;;
        notifications) echo "src/shell/Notifications.h" ;;
        agents) echo "src/agents/AgentsFacade.h" ;;
        web) echo "src/web/WebTabsFacade.h" ;;
        skills) echo "src/skills/SkillsFacade.h" ;;
        workbench) echo "src/workbench/WorkbenchContext.h" ;;
        environment) echo "src/workbench/EnvironmentService.h" ;;
        WebProfiles) echo "src/web/webengine/WebEngineProfileStore.h" ;;
        agents.model) echo "src/agents/AgentModel.h" ;;
        skills.model) echo "src/skills/SkillModel.h" ;;
        web.model) echo "src/web/WebTabsModel.h" ;;
        *) echo "" ;;
    esac
}

ALIASES="theme nav shell ui notifications agents web skills workbench environment WebProfiles agents.model skills.model web.model"
QML_FILES="$(find qml src examples -name '*.qml' 2>/dev/null)"
for alias in $ALIASES; do
    hdr="$(alias_header "$alias")"
    [ -n "$hdr" ] || continue
    # Flatten to one line AND strip line comments first — otherwise a
    # comment mentioning Q_INVOKABLE next to a method name creates a false
    # pass (comments are not declarations).
    flat="$(sed -E 's|//.*||' "$hdr" | tr '\n' ' ')"
    alias_re="${alias//./\\.}"

    # (a) method calls: alias.method( — must be Q_INVOKABLE in that header.
    # QAbstractItemModel's own Q_INVOKABLEs are grandfathered for *.model.
    while IFS= read -r m; do
        [ -n "$m" ] || continue
        if printf '%s' "$flat" | grep -qP "Q_INVOKABLE[^;]*\b\Q$m\E\s*\("; then
            continue
        fi
        case "$alias" in
            *.model)
                case "$m" in rowCount|index|parent|headerData) continue ;; esac
                ;;
        esac
        fail "QML calls ${alias}.${m}() but ${hdr} has no Q_INVOKABLE ${m}(…) — QML cannot invoke a plain method (TypeError at click time)"
    done < <(grep -rhoP "\b${alias_re}\.\K[A-Za-z_][A-Za-z0-9_]*(?=\s*\()" $QML_FILES 2>/dev/null | sort -u)

    # (b) property writes: alias.prop = — must have a WRITE accessor.
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        if printf '%s' "$flat" | grep -qP "Q_PROPERTY[^)]*\b\Q$p\E\b[^)]*WRITE"; then
            continue
        fi
        fail "QML assigns ${alias}.${p} = … but ${hdr} has no WRITE accessor (read-only property; TypeError at click time)"
    done < <(grep -rhoP "\b${alias_re}\.\K[A-Za-z_][A-Za-z0-9_]*(?=\s*=(?!=))" $QML_FILES 2>/dev/null | sort -u)
done

if [[ $status -eq 0 ]]; then
    echo "check-architecture: OK"
fi
exit $status
