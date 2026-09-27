# L4 executable: assembly + the QML module
# + every embedded resource. No business logic lives here.
#
# All module QML is compiled into the executable through qt_add_qml_module
# with stable aliases, so a page URL looks like
#   qrc:/qt/qml/AgentWorkbench/agentcatalog/AgentGridPage.qml
# Resources are never put into static libraries (qrc initializers in static
# libraries get dropped by the linker.

set(_shell_qml MainWindow Sidebar Workspace StatusBar Toasts PageHeader
               SettingsPage)
set(_component_qml AButton AIconButton ASearchField ACard APill ADialog
                   AConfirmDialog AAlertDialog AEmptyState ASectionHeader
                   AToastStack AStatusDot ATextField AFormLabel AListRow
                   AgentAvatar)
set(_agentcatalog_qml AgentCard AgentGridPage AgentEditDialog)
set(_skillcatalog_qml SkillGridPage SkillCard SkillDetailFlyout)
set(_web_qml WebTabsPage)
set(_webengine_qml WebEngineSurface)

set(AWB_QML_FILES "")
foreach(name IN LISTS _skillcatalog_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/skillcatalog/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "skillcatalog/${name}.qml")
endforeach()
foreach(name IN LISTS _web_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/web/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "web/${name}.qml")
endforeach()
foreach(name IN LISTS _webengine_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/web/webengine/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "web/${name}.qml")
endforeach()
foreach(name IN LISTS _shell_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/shell/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "shell/${name}.qml")
endforeach()
foreach(name IN LISTS _component_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/shell/qml/components/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "components/${name}.qml")
endforeach()
foreach(name IN LISTS _agentcatalog_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/agentcatalog/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "agentcatalog/${name}.qml")
endforeach()

qt_add_executable(AgentWorkbench
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/app.rc
)

target_include_directories(AgentWorkbench
    PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(AgentWorkbench PRIVATE
    awb_agentcatalog
    awb_core
    awb_shell
    awb_skillcatalog
    awb_theme
    awb_web
    awb_workbench
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
)

if (AWB_ENABLE_WEBENGINE)
    target_compile_definitions(AgentWorkbench PRIVATE AWB_ENABLE_WEBENGINE=1)
    target_link_libraries(AgentWorkbench PRIVATE awb_web_webengine
                                                 Qt6::WebEngineQuick)
endif()

# The QML module shared by every page/component.
qt_add_qml_module(AgentWorkbench
    URI AgentWorkbench
    VERSION 1.0
    NO_CACHEGEN
    QML_FILES ${AWB_QML_FILES}
)

# --- Embedded resources (executable only, never a library) -------------------
set(AWB_ICON_FILES
    ${CMAKE_SOURCE_DIR}/icons/app-icon.png
    ${CMAKE_SOURCE_DIR}/icons/bot.svg
    ${CMAKE_SOURCE_DIR}/icons/chevron-down.svg
    ${CMAKE_SOURCE_DIR}/icons/chevron-left.svg
    ${CMAKE_SOURCE_DIR}/icons/chevron-right.svg
    ${CMAKE_SOURCE_DIR}/icons/close.svg
    ${CMAKE_SOURCE_DIR}/icons/copy.svg
    ${CMAKE_SOURCE_DIR}/icons/cube.svg
    ${CMAKE_SOURCE_DIR}/icons/default.svg
    ${CMAKE_SOURCE_DIR}/icons/deepseek-harness.svg
    ${CMAKE_SOURCE_DIR}/icons/download-hover.svg
    ${CMAKE_SOURCE_DIR}/icons/download.svg
    ${CMAKE_SOURCE_DIR}/icons/external-link.svg
    ${CMAKE_SOURCE_DIR}/icons/folder.svg
    ${CMAKE_SOURCE_DIR}/icons/gear.svg
    ${CMAKE_SOURCE_DIR}/icons/home.svg
    ${CMAKE_SOURCE_DIR}/icons/kimi-code.svg
    ${CMAKE_SOURCE_DIR}/icons/menu.svg
    ${CMAKE_SOURCE_DIR}/icons/openclaw.svg
    ${CMAKE_SOURCE_DIR}/icons/opencode.svg
    ${CMAKE_SOURCE_DIR}/icons/plus.svg
    ${CMAKE_SOURCE_DIR}/icons/qwen-code.svg
    ${CMAKE_SOURCE_DIR}/icons/refresh.svg
    ${CMAKE_SOURCE_DIR}/icons/search.svg
    ${CMAKE_SOURCE_DIR}/icons/skills.svg
    ${CMAKE_SOURCE_DIR}/icons/terminal.svg
    ${CMAKE_SOURCE_DIR}/icons/web.svg
)
foreach(icon IN LISTS AWB_ICON_FILES)
    get_filename_component(_name ${icon} NAME)
    set_source_files_properties(${icon} PROPERTIES
        QT_RESOURCE_ALIAS "icons/${_name}")
endforeach()

set_source_files_properties(${CMAKE_SOURCE_DIR}/config/default_agents.json
    PROPERTIES QT_RESOURCE_ALIAS "config/default_agents.json")

qt_add_resources(AgentWorkbench "app_resources"
    PREFIX "/"
    FILES
        ${CMAKE_SOURCE_DIR}/config/default_agents.json
        ${AWB_ICON_FILES}
)

# Built-in themes.
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
    PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")

qt_add_resources(AgentWorkbench "theme_resources"
    PREFIX "/themes"
    FILES
        ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
        ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
)

# Win32 GUI subsystem (no console window) on Windows.
set_target_properties(AgentWorkbench PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
