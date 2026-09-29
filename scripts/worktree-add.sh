#!/usr/bin/env bash
# AgentWorkbench worktree helper — parallel-development worktrees under
# .worktree/ with submodules initialized offline.
#
# Why plain git commands are not enough on this machine:
#   * `git worktree add` leaves submodule directories empty, and a bare
#     `git submodule update --init` in the new worktree tries to clone the
#     .gitmodules URL (gitee.com), which the intranet cannot reach. This
#     script overrides that URL at runtime (`git -c submodule.<name>.url=…`,
#     no config pollution) with an already-populated checkout from another
#     worktree, so the clone happens offline and is near-instant (hardlinked
#     objects). Each worktree gets its own submodule gitdir under
#     .git/worktrees/<id>/modules/, so worktrees never share submodule state
#     with each other or with the main checkout.
#   * The local git (2.7.2) has no `git worktree remove`, so --remove does
#     the equivalent: rm -rf the registered worktree + `git worktree prune`.
#
# Usage:
#   bash scripts/worktree-add.sh <branch> [base]    create a worktree
#   bash scripts/worktree-add.sh --remove <name>    remove a worktree
#   bash scripts/worktree-add.sh --list             list worktrees
#   bash scripts/worktree-add.sh --help             show this help
#
# Create:
#   <branch>  branch to work on (feat/<domain>-<topic> or fix/<topic> per
#             AGENTS.md). It is created from [base] (default: dev); if it
#             already exists, it is simply checked out. The worktree lands
#             in <main-repo>/.worktree/<branch with '/' replaced by '-'>
#             no matter which worktree the script is invoked from;
#             .worktree/ is git-ignored.
#   Note: the submodule commit recorded by <base> must exist in the donor
#   checkout (normally the main one). If <base> moves to a commit that
#   bumps a submodule, run `git submodule update` in the main worktree
#   first — on the intranet that donor checkout is the only object source.
#
# --remove <name>:
#   <name> is the worktree directory name under .worktree/ (e.g.
#   feat-web-xyz); a full or relative path also works. Only registered
#   worktrees are ever deleted, never the main checkout. The branch is
#   kept; delete it with `git branch -d <branch>` once merged.

set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }

usage() { sed -n '2,/^set -euo/p' "$0" | sed 's/^#//; s/^ //; $d'; }

# Run from the repository the script file lives in (any worktree of it),
# so the script also works when invoked by absolute path from outside.
cd "$(dirname "$0")/.." || die "cannot enter the repository root"
git rev-parse --git-dir >/dev/null 2>&1 || die "not inside a git repository"

# Every worktree path, main worktree first (porcelain output is stable).
worktree_paths() { git worktree list --porcelain | sed -n 's/^worktree //p'; }

MAIN_WT=$(worktree_paths | head -n1)
cd "$MAIN_WT"

# Populate every submodule of the worktree at $1 offline, cloning from the
# first worktree that already has that submodule checked out.
init_submodules() {
    local wt=$1
    local modfile="$wt/.gitmodules"
    if [ ! -f "$modfile" ]; then
        return 0
    fi
    local key path name sha donor p
    while read -r key path; do
        name=${key#submodule.}
        name=${name%.path}
        sha=$(git -C "$wt" ls-tree HEAD "$path" | awk '$1 == "160000" {print $3}')
        if [ -z "$sha" ]; then
            continue    # not recorded in this tree (removed upstream)
        fi
        if [ "$(git -C "$wt/$path" rev-parse --verify --quiet HEAD 2>/dev/null)" = "$sha" ]; then
            echo "  submodule: $path @ ${sha:0:7} (already present)"
            continue
        fi
        donor=""
        while IFS= read -r p; do
            if [ -e "$p/$path/.git" ]; then
                donor="$p/$path"
                break
            fi
        done < <(worktree_paths)
        if [ -z "$donor" ]; then
            die "no populated checkout of '$path' in any worktree — initialize it in the main worktree first (needs network once): git submodule update --init $path"
        fi
        # protocol.file.allow：git ≥2.38.1 默认拒绝 file:// 协议的子模块
        # 克隆（CVE-2022-39253 缓解），本地 donor 路径是合法用途，显式放行。
        git -C "$wt" -c "submodule.$name.url=$donor" -c protocol.file.allow=always \
            submodule update --init "$path" >&2 \
            || die "offline submodule init failed for '$path' (donor: $donor). If the donor lacks commit $sha, fetch it there first (needs network) or pick a base the donor covers."
        echo "  submodule: $path @ ${sha:0:7} (cloned from $donor)"
    done < <(git config -f "$modfile" --get-regexp '^submodule\..*\.path$')
}

cmd_add() {
    local branch=${1:-} base=${2:-dev}
    if [ -z "$branch" ]; then
        die "missing <branch> (see --help)"
    fi
    local dirname=${branch//\//-}
    local wt="$MAIN_WT/.worktree/$dirname"
    if [ -e "$wt" ]; then
        die "worktree directory already exists: $wt"
    fi
    if git rev-parse --verify --quiet "refs/heads/$branch" >/dev/null; then
        echo "Branch '$branch' already exists — checking it out."
        git worktree add "$wt" "$branch"
    else
        git rev-parse --verify --quiet "$base^{commit}" >/dev/null \
            || die "base ref not found: $base"
        git worktree add -b "$branch" "$wt" "$base"
    fi
    init_submodules "$wt"
    echo
    echo "Worktree ready:"
    echo "  path:    $wt"
    echo "  branch:  $branch"
    echo "Next steps:"
    echo "  cd \"$wt\""
    echo "  bash scripts/build.sh --test"
}

cmd_remove() {
    local name=${1:-}
    if [ -z "$name" ]; then
        die "--remove needs a worktree name (see --list)"
    fi
    local base
    base=$(basename "$name")
    local target="" p
    while IFS= read -r p; do
        if [ "$p" = "$MAIN_WT" ]; then
            continue
        fi
        if [ "$p" = "$name" ] || [ "$(basename "$p")" = "$base" ]; then
            target=$p
            break
        fi
    done < <(worktree_paths)
    if [ -z "$target" ]; then
        die "no such worktree: $name (see --list)"
    fi
    rm -rf "$target" \
        || die "cannot delete $target — a process (shell, editor, build) still uses it; leave that directory and retry"
    git worktree prune
    echo "Removed worktree: $target"
    echo "Its branch (if any) was kept — delete it with 'git branch -d <branch>' once merged."
}

case ${1:-} in
    -h|--help)
        usage
        ;;
    --list)
        git worktree list
        ;;
    --remove)
        shift
        cmd_remove "$@"
        ;;
    "")
        usage
        exit 1
        ;;
    *)
        cmd_add "$@"
        ;;
esac
