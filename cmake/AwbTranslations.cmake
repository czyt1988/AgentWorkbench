# Translatable source list for lupdate/lrelease.
# One .ts file (translations/agentworkbench_zh_CN.ts) covers the whole app;
# every source with tr()/translate()/qsTr() must appear here, so add new
# files to this list rather than to the qt6_create_translation() call.
#
# Paths are anchored at CMAKE_SOURCE_DIR so the list can be included from
# any CMakeLists.txt in the project.

set(AWB_TS_SOURCES
    # executable assembly
    ${CMAKE_SOURCE_DIR}/app/main.cpp
    # core
    ${CMAKE_SOURCE_DIR}/src/core/LegacyImport.cpp
    # agent catalog
    ${CMAKE_SOURCE_DIR}/src/agentcatalog/AgentRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/agentcatalog/AgentScripts.cpp
    ${CMAKE_SOURCE_DIR}/src/agentcatalog/AgentsFacade.cpp
    # shell
    ${CMAKE_SOURCE_DIR}/src/shell/UiServices.cpp
    # application layer
    ${CMAKE_SOURCE_DIR}/src/workbench/BuiltinPages.cpp
    ${CMAKE_SOURCE_DIR}/src/workbench/PluginServices.cpp
    ${CMAKE_SOURCE_DIR}/src/workbench/WorkbenchContext.cpp
    # shell QML
    ${CMAKE_SOURCE_DIR}/src/shell/qml/MainWindow.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/Sidebar.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/Workspace.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/StatusBar.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/Toasts.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/PageHeader.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsPage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsAppearancePage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsLaunchersPage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsEnvironmentPage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsSkillsPage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsWebPage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsPluginsPage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/SettingsAdvancedPage.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AIconButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASearchField.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ACard.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/APill.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ADialog.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AEmptyState.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASectionHeader.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AToastStack.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AStatusDot.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextField.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AFormLabel.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AgentAvatar.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AColorPicker.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AColorField.qml
    # agent catalog QML
    ${CMAKE_SOURCE_DIR}/src/agentcatalog/qml/AgentCard.qml
    ${CMAKE_SOURCE_DIR}/src/agentcatalog/qml/AgentGridPage.qml
    ${CMAKE_SOURCE_DIR}/src/agentcatalog/qml/AgentEditDialog.qml
    # web QML
    ${CMAKE_SOURCE_DIR}/src/web/qml/WebTabsPage.qml
    ${CMAKE_SOURCE_DIR}/src/web/webengine/qml/WebEngineSurface.qml
    # theme
    ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
    # skill catalog + QML
    ${CMAKE_SOURCE_DIR}/src/skillcatalog/SkillsFacade.cpp
    ${CMAKE_SOURCE_DIR}/src/skillcatalog/qml/SkillGridPage.qml
    ${CMAKE_SOURCE_DIR}/src/skillcatalog/qml/SkillCard.qml
    ${CMAKE_SOURCE_DIR}/src/skillcatalog/qml/SkillDetailFlyout.qml
    # tools
    ${CMAKE_SOURCE_DIR}/src/tools/ToolsFacade.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/qml/ToolsPage.qml
    ${CMAKE_SOURCE_DIR}/src/tools/qml/MarkdownContextMenu.qml
)
