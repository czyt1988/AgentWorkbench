# Vendored: SingleApplication

Replacement for the unmaintained `QtSingleApplication` — the single-instance
guard used by `app/main.cpp`. Qt itself has no single-instance class, only the
primitives (`QSharedMemory`, `QLocalServer`/`QLocalSocket`) this library is
built from.

| | |
|---|---|
| Upstream | https://github.com/itay-grudev/SingleApplication |
| Tag | `v3.5.6` (project version 3.6.0) |
| Commit | `a218603d76a55dddb65b84f2b49ecaa8efc074ea` |
| License | MIT with an added attribution clause — see `LICENSE` |
| Files kept | `singleapplication.{h,cpp}`, `singleapplication_p.{h,cpp}`, `CMakeLists.txt`, `LICENSE` |

Files are byte-identical to the upstream tag; nothing is patched.

## Why a checked-in copy instead of a submodule

`git` cannot reach GitHub from the networks this project builds on (the
`third_party/spdlog` submodule already had to move to a mirror for the same
reason). A vendored copy keeps a fresh clone — and every worktree under
`.worktree/` — buildable with no network at all, which is the property
`AGENTS.md` treats as a hard requirement.

## License obligations

The license is permissive (MIT-shaped) but **conditional**: redistributing the
software, a derivative work, or any associated file requires including the
upstream license text. `LICENSE` is kept next to the sources for exactly that
reason, and must stay in the tree in any redistributed build of the
application. It additionally withholds permission to use these files as
training data for machine-learning models.

`CMakeLists.txt` also carries a Doxygen branch that would `FetchContent` a
theme from GitHub. It is only reachable when `SINGLEAPPLICATION_DOCUMENTATION`
is ON, which the top-level build never enables.

## Updating

Re-download the tarball of a newer tag, replace the four sources plus
`CMakeLists.txt` and `LICENSE`, and re-verify both Qt routes
(`bash scripts/build.sh --test` on Qt 6, and the Qt 5.15.16 fallback build).
The library keeps its own `QT_VERSION` branches; the project sets
`QAPPLICATION_CLASS` to `QApplication` from the top-level `CMakeLists.txt`.
