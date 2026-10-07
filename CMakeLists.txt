cmake_minimum_required(VERSION 3.16)

project(AgentWorkbench VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt discovery: Qt 6.5+ preferred, Qt 5.15 LTS as the fallback (5.15.16 is
# the verified floor — the WebEngine backports the embedded Web surface uses
# live in the LTS patch releases). The per-major minimum is enforced by the
# second find_package; QT_VERSION_MAJOR drives every version branch, see
# cmake/AwbQtCompat.cmake. Pass -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64
# if not auto-detected.
# Widgets 只服务系统托盘（shell::TrayController 用的 QSystemTrayIcon 是这个
# 模块的类），它同时是 SingleApplication 的 QAPPLICATION_CLASS。
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
if (QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 6.5 REQUIRED
        COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent Widgets
                   LinguistTools)
    qt_standard_project_setup(REQUIRES 6.5)
else()
    find_package(Qt5 5.15 REQUIRED
        COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent Widgets
               LinguistTools)
endif()

# Build options: AWB_ENABLE_WEBENGINE etc. Must come before
# the subdirectories that branch on them.
include(cmake/AwbOptions.cmake)

# Third-party: spdlog, the async backend of core::Logging. It is a git
# submodule; fail early with the fix instead of a confusing error from
# add_subdirectory. WCHAR filenames are required: non-ASCII user profiles
# (C:\Users\陈宗衍\…) must open through wide-char APIs, never narrow ones.
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/CMakeLists.txt")
    message(FATAL_ERROR
        "spdlog submodule is missing. Run:\n"
        "  git submodule update --init third_party/spdlog")
endif()
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_INSTALL OFF)
set(SPDLOG_WCHAR_FILENAMES ON)
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

# Fill QMessageLogContext (file:line/function) for every qInfo/qWarning call
# so source locations stay in the log in release builds too.
add_compile_definitions(QT_MESSAGELOGCONTEXT)

# Version-dispatching helpers (awb_add_resources, awb_add_qml_module, the
# WebEngine link target). Included AFTER spdlog: the Qt 5 branch injects
# /utf-8, which must not reach the Qt-less third-party target above, and
# must precede every project target below.
include(cmake/AwbQtCompat.cmake)

# Third-party: SingleApplication, the single-instance guard used by
# app/main.cpp. Vendored (checked-in copy at tag v3.5.6), NOT a submodule:
# GitHub is unreachable over git on the networks this project builds on, and a
# copy keeps a fresh clone and every .worktree/ build offline. Provenance and
# license terms: third_party/SingleApplication/UPSTREAM.md.
#
# Its own CMakeLists branches on QT_DEFAULT_MAJOR_VERSION and needs the Qt
# package already found, hence it sits after the find_package calls above and
# after AwbQtCompat (the Qt 5 /utf-8 option is harmless here - the vendored
# sources are pure ASCII, but the target does link Qt).
# QAPPLICATION_CLASS picks the base class: QApplication, because the tray icon
# (Qt Widgets) needs it and the app now runs on it.
set(QT_DEFAULT_MAJOR_VERSION ${QT_VERSION_MAJOR}
    CACHE STRING "Qt major for SingleApplication's own find_package")
set(QAPPLICATION_CLASS QApplication CACHE STRING
    "Base class of SingleApplication - QApplication for the tray icon")
set(SINGLEAPPLICATION_INSTALL OFF)
set(SINGLEAPPLICATION_DOCUMENTATION OFF)
add_subdirectory(third_party/SingleApplication EXCLUDE_FROM_ALL)

# Third-party: SARibbon, the ribbon UI of the Markdown workbench page. It is
# a git submodule (same convention as spdlog; offline init via
# scripts/worktree-add.sh, which clones from a populated checkout). Pinned to
# the upstream dev-3.0 tip d3d45d8 (2026-10-07) — it carries the ribbon fixes
# this integration needs: panel child order scrambling, in-panel separators
# never rendering, hit-test visibility of registered categories. From c7f9bfd
# the QML module hard-requires QWindowKit; SARibbon's CMake builds it in-tree
# from its own nested submodule (3rdparty/qwindowkit — init it offline from
# C:/src/Qt/SARibbon/3rdparty/qwindowkit, see below). Linking QWK does NOT
# make the frameless window mandatory: the page keeps the native window frame
# (no RibbonWindowAgent is declared anywhere).
#
# Only the QML module is built (SARIBBON_BUILD_WIDGETS=OFF keeps the QWidget
# stack out of the binary); static libs avoid a third DLL next to the exe.
# SARIBBON_INSTALL/SARIBBON_INSTALL_IN_CURRENT_DIR=OFF keep its install/export
# rules from polluting this project. A missing submodule fails loudly with the
# fix instead of silently building a ribbon-less binary (AWB_ENABLE_RIBBON=OFF
# is the only supported way to trim it).
if (AWB_ENABLE_RIBBON)
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/SARibbon/CMakeLists.txt")
        message(FATAL_ERROR
            "SARibbon submodule is missing. Run:\n"
            "  git submodule update --init third_party/SARibbon\n"
            "(on this machine's intranet, use bash scripts/worktree-add.sh, "
            "which clones it offline from the main worktree)")
    endif()
    if (NOT EXISTS
            "${CMAKE_SOURCE_DIR}/third_party/SARibbon/3rdparty/qwindowkit/CMakeLists.txt")
        message(FATAL_ERROR
            "SARibbon's nested qwindowkit submodule is missing (the QML "
            "module hard-requires it since c7f9bfd). Run inside "
            "third_party/SARibbon:\n"
            "  git -c submodule.3rdparty/qwindowkit.url="
            "<populated qwindowkit checkout> \\\n"
            "    submodule update --init 3rdparty/qwindowkit\n"
            "(offline: point the URL at C:/src/Qt/SARibbon/3rdparty/"
            "qwindowkit)")
    endif()
    set(SARIBBON_BUILD_QML ON)
    set(SARIBBON_BUILD_WIDGETS OFF)
    set(SARIBBON_BUILD_EXAMPLES OFF)
    set(SARIBBON_BUILD_TESTS OFF)
    set(SARIBBON_INSTALL OFF)
    set(SARIBBON_INSTALL_IN_CURRENT_DIR OFF)
    set(SARIBBON_BUILD_STATIC_LIBS ON)
    add_subdirectory(third_party/SARibbon EXCLUDE_FROM_ALL)
    # qmsetup's forwarding headers use ../../ chains anchored at the build
    # directory; in deep shadow build dirs (Qt Creator's default) the path
    # cl.exe must open exceeds MAX_PATH and every QWK compile dies with a
    # bogus C1083. Rewrite them to absolute paths — see the function docs.
    include(cmake/AwbThirdParty.cmake)
    awb_absolutize_forwarding_headers(
        "${CMAKE_BINARY_DIR}/third_party/SARibbon")
endif()

# Layered modules, bottom-up:
#   core -> theme -> {shell, agentcatalog, skillcatalog, web, tools} -> workbench -> app
add_subdirectory(src/plugin_api) # L0 plugin ABI (header-only)
add_subdirectory(src/core)      # L0 infrastructure
add_subdirectory(src/theme)     # L1 theme engine
add_subdirectory(src/agentcatalog) # L2 agent catalog
add_subdirectory(src/shell)     # L2 window skeleton (UI framework)
add_subdirectory(src/web)       # L2 web tabs (+ webengine adapter when ON)
add_subdirectory(src/skillcatalog) # L2 skill catalog
add_subdirectory(src/tools)     # L2 agent tools (workspace memory, prompt draft)
add_subdirectory(src/markdown)  # L2 markdown preview (state layer; adapter when ON)
add_subdirectory(src/workbench) # L3 application layer

# Translations. The default build only runs lrelease (.ts -> .qm) via
# qt_add_translation — lupdate (source -> .ts sync) rewrites <location> line
# numbers on every run, which used to leave the .ts perpetually modified in
# the worktree. Syncing the .ts is now an explicit step instead:
# scripts/update-ts.sh (or cmake --build build --target update_ts), to be run
# before committing a tr()/qsTr() source string change. The scan list lives
# in cmake/AwbTranslations.cmake.
include(cmake/AwbTranslations.cmake)
qt_add_translation(QM_FILES
    translations/agentworkbench_zh_CN.ts
)
# Order-only target: the .qm custom commands live in this directory while the
# resource embedding happens in app/; the exe depends on this target so MSBuild
# cannot run rcc before lrelease.
add_custom_target(awb_translations DEPENDS ${QM_FILES})
# Explicit .ts sync (NOT part of the default build). lupdate reads the source
# list through an @file (90+ paths exceed the Windows command-line limit);
# the list file is regenerated at configure time, so it never goes stale.
set(AWB_LUPDATE_LIST_FILE "${CMAKE_BINARY_DIR}/awb_ts_sources.lst")
string(REPLACE ";" "\n" AWB_TS_SOURCES_LINES "${AWB_TS_SOURCES}")
file(WRITE "${AWB_LUPDATE_LIST_FILE}" "${AWB_TS_SOURCES_LINES}\n")
if (QT_VERSION_MAJOR EQUAL 6)
    # Qt 6 ships lupdate as the Qt6::lupdate executable target.
    set(AWB_LUPDATE_EXECUTABLE Qt6::lupdate)
else()
    set(AWB_LUPDATE_EXECUTABLE ${Qt5_LUPDATE_EXECUTABLE})
endif()
add_custom_target(update_ts
    COMMAND ${AWB_LUPDATE_EXECUTABLE} "@${AWB_LUPDATE_LIST_FILE}"
            -ts "${CMAKE_SOURCE_DIR}/translations/agentworkbench_zh_CN.ts"
    VERBATIM)
# L4 executable: assembly + QML module + all embedded resources.
add_subdirectory(app)

# --- Unit tests ---------------------------------------------------------------
# Isolated from the real user config via QStandardPaths test mode (see
# tests/). One executable per module, registered with ctest under the same
# name. Disable with -DBUILD_TESTING=OFF (e.g. if the Qt
# installation lacks the Test component).
option(BUILD_TESTING "Build the unit tests" ON)

if (BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
