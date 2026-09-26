cmake_minimum_required(VERSION 3.16)

project(AgentWorkbench VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt6 discovery. Pass -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64 if not set.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network LinguistTools)

qt_standard_project_setup(REQUIRES 6.5)

set(APP_SOURCES
    src/main.cpp
    src/AgentConfig.cpp
    src/AgentModel.cpp
    src/AgentLauncher.cpp
    src/Logger.cpp
    src/core/LegacyImport.cpp
)

set(APP_HEADERS
    src/AgentConfig.h
    src/AgentModel.h
    src/AgentLauncher.h
    src/Logger.h
    src/core/LegacyImport.h
)

qt_add_executable(AgentWorkbench
    ${APP_SOURCES}
    ${APP_HEADERS}
    src/app.rc
)

# Include root for spec-style includes ("core/Foo.h", 01-architecture.md §6).
target_include_directories(AgentWorkbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

qt_add_resources(AgentWorkbench "app_resources"
    PREFIX "/"
    FILES
        config/default_agents.json
        icons/kimi-code.svg
        icons/opencode.svg
        icons/openclaw.svg
        icons/deepseek-harness.svg
        icons/qwen-code.svg
        icons/default.svg
        icons/terminal.svg
        icons/cube.svg
        icons/bot.svg
        icons/download.svg
        icons/download-hover.svg
        icons/gear.svg
        icons/app-icon.png
        qml/main.qml
        qml/AgentCard.qml
        qml/AgentEditPage.qml
        qml/SettingsPage.qml
)

target_link_libraries(AgentWorkbench PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
)

# Translations: lupdate syncs .ts from source, lrelease compiles .qm.
# qt6_create_translation auto-separates .ts files from sources by extension.
qt6_create_translation(QM_FILES
    ${APP_SOURCES}
    ${APP_HEADERS}
    qml/main.qml
    qml/AgentCard.qml
    qml/AgentEditPage.qml
    qml/SettingsPage.qml
    translations/agentworkbench_zh_CN.ts
)

# Generated .qm files have absolute paths; alias them to relative names so
# qt_add_resources can embed them under :/i18n/.
set_source_files_properties(${QM_FILES} PROPERTIES
    QT_RESOURCE_ALIAS "agentworkbench_zh_CN.qm"
)

qt_add_resources(AgentWorkbench "i18n"
    PREFIX "/i18n"
    FILES
        ${QM_FILES}
)

# Win32 GUI subsystem (no console window) on Windows.
set_target_properties(AgentWorkbench PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# --- Unit tests ---------------------------------------------------------------
# Isolated from the real user config via QStandardPaths test mode (see
# tests/tst_core.cpp). Disable with -DBUILD_TESTING=OFF (e.g. if the Qt
# installation lacks the Test component).
option(BUILD_TESTING "Build the unit test target" ON)

if (BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(AgentWorkbenchTests
        tests/tst_core.cpp
        src/AgentConfig.cpp
        src/AgentModel.cpp
        src/AgentLauncher.cpp
        src/Logger.cpp
        src/core/LegacyImport.cpp
    )
    # tst_core.cpp includes "AgentConfig.h" from src/.
    target_include_directories(AgentWorkbenchTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    qt_add_resources(AgentWorkbenchTests "test_resources"
        PREFIX "/"
        FILES
            config/default_agents.json
    )
    target_link_libraries(AgentWorkbenchTests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Test
    )
    add_test(NAME AgentWorkbenchTests COMMAND AgentWorkbenchTests)
endif()
