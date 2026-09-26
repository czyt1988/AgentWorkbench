cmake_minimum_required(VERSION 3.16)

project(AgentWorkbench VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt6 discovery. Pass -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64 if not set.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network LinguistTools)

qt_standard_project_setup(REQUIRES 6.5)

# Build options (specs/01 §10): AWB_ENABLE_WEBENGINE etc. Must come before
# the subdirectories that branch on them.
include(cmake/AwbOptions.cmake)

# Layered modules, bottom-up (specs/01-architecture.md §3):
#   core -> theme -> {shell, agents, web} -> workbench -> app
add_subdirectory(src/core)      # L0 infrastructure
add_subdirectory(src/theme)     # L1 theme engine
add_subdirectory(src/agents)    # L2 agents domain
add_subdirectory(src/shell)     # L2 window skeleton (UI framework)
add_subdirectory(src/web)       # L2 web tabs (+ webengine adapter when ON)
add_subdirectory(src/workbench) # L3 application layer

# Translations: lupdate syncs .ts from source, lrelease compiles .qm.
# The scan list lives in cmake/AwbTranslations.cmake (specs/01 §10).
include(cmake/AwbTranslations.cmake)
qt6_create_translation(QM_FILES
    ${AWB_TS_SOURCES}
    translations/agentworkbench_zh_CN.ts
)
# L4 executable: assembly + QML module + all embedded resources (§4.9).
add_subdirectory(app)

# Compiled translations, embedded under :/i18n/. Kept in the same directory
# as the qt6_create_translation call above: the generated .qm's custom
# command and its resource must share a directory scope, and the alias is
# directory-scoped as well.
set_source_files_properties(${QM_FILES} PROPERTIES
    QT_RESOURCE_ALIAS "agentworkbench_zh_CN.qm")

qt_add_resources(AgentWorkbench "i18n"
    PREFIX "/i18n"
    FILES
        ${QM_FILES}
)

# --- Unit tests ---------------------------------------------------------------
# Isolated from the real user config via QStandardPaths test mode (see
# tests/). One executable per module, registered with ctest under the same
# name (specs/01 §11). Disable with -DBUILD_TESTING=OFF (e.g. if the Qt
# installation lacks the Test component).
option(BUILD_TESTING "Build the unit tests" ON)

if (BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
