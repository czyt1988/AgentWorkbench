# Translatable source list for lupdate/lrelease (specs/01-architecture.md §10).
# One .ts file (translations/agentworkbench_zh_CN.ts) covers the whole app;
# every source with tr()/translate()/qsTr() must appear here, so add new
# files to this list rather than to the qt6_create_translation() call.
#
# Paths are anchored at CMAKE_SOURCE_DIR so the list can be included from
# any CMakeLists.txt in the project.

set(AWB_TS_SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LegacyImport.cpp
    ${CMAKE_SOURCE_DIR}/src/agents/AgentRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/agents/AgentScripts.cpp
    ${CMAKE_SOURCE_DIR}/src/agents/AgentsFacade.cpp
    ${CMAKE_SOURCE_DIR}/qml/main.qml
    ${CMAKE_SOURCE_DIR}/qml/AgentCard.qml
    ${CMAKE_SOURCE_DIR}/qml/AgentEditPage.qml
    ${CMAKE_SOURCE_DIR}/qml/SettingsPage.qml
)
