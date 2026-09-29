# tst_tools — workspace store, lazy file tree model (tree + flat projection),
# icon map, facade wiring, markdown editing support.
add_executable(tst_tools
    tst_tools_main.cpp
    tst_toolsstore.cpp
    tst_filetreemodel.cpp
    tst_filetreeflatmodel.cpp
    tst_fileicons.cpp
    tst_toolsfacade.cpp
    tst_markdownedit.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_tools PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# The bundled icon map is an absolute path outside this directory; the alias
# must be declared before awb_add_resources() processes the file. FileIcons
# reads it from the same :/config/ URL the executable uses.
set(_tst_tools_default_icons ${CMAKE_SOURCE_DIR}/config/default_file_icons.json)
set_source_files_properties(${_tst_tools_default_icons}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_file_icons.json")

awb_add_resources(tst_tools "test_resources"
    PREFIX "/"
    FILES
        ${_tst_tools_default_icons}
)

target_link_libraries(tst_tools PRIVATE
    awb_tools
    awb_core
    Qt::Core
    Qt::Qml
    Qt::Test
)

add_test(NAME tst_tools COMMAND tst_tools)

# tst_toolsui — interactive smoke for the Agent Tools page (real mouse +
# keyboard against a real window). The page and every component it references
# are embedded under the same module URLs/aliases as app/CMakeLists.txt, so
# the engine resolves exactly what the Workspace Loader loads. A new
# component dependency fails loudly here ("unknown type") — add it below.
# Needs its own main (QGuiApplication drives window event delivery; the shared
# registry runner only provides QCoreApplication).
add_executable(tst_toolsui
    tst_toolsui.cpp
)

target_include_directories(tst_toolsui PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(tst_toolsui PRIVATE
    awb_tools
    awb_theme
    awb_core
    Qt::Core
    Qt::Gui
    Qt::Qml
    Qt::Quick
    Qt::QuickControls2
    Qt::Test
)

# 内置主题与文件树图标表（与 app 相同的 :/ URL）。alias 必须在
# awb_add_resources 处理文件之前声明。
set(_tst_toolsui_themes
    ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json)
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
    PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")
awb_add_resources(tst_toolsui "test_themes"
    PREFIX "/themes"
    FILES
        ${_tst_toolsui_themes}
)

set(_tst_toolsui_default_icons ${CMAKE_SOURCE_DIR}/config/default_file_icons.json)
set_source_files_properties(${_tst_toolsui_default_icons}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_file_icons.json")

# 页面与组件引用的全部图标（alias = icons/<name>，URL 与 app 一致）。
set(_tst_toolsui_icons
    ${CMAKE_SOURCE_DIR}/icons/default.svg
    ${CMAKE_SOURCE_DIR}/icons/folder.svg
    ${CMAKE_SOURCE_DIR}/icons/file.svg
    ${CMAKE_SOURCE_DIR}/icons/plus.svg
    ${CMAKE_SOURCE_DIR}/icons/refresh.svg
    ${CMAKE_SOURCE_DIR}/icons/close.svg
    ${CMAKE_SOURCE_DIR}/icons/copy.svg
    ${CMAKE_SOURCE_DIR}/icons/paste.svg
    ${CMAKE_SOURCE_DIR}/icons/cut.svg
    ${CMAKE_SOURCE_DIR}/icons/undo.svg
    ${CMAKE_SOURCE_DIR}/icons/redo.svg
    ${CMAKE_SOURCE_DIR}/icons/bold.svg
    ${CMAKE_SOURCE_DIR}/icons/code.svg
    ${CMAKE_SOURCE_DIR}/icons/bullet-list.svg
    ${CMAKE_SOURCE_DIR}/icons/chevron-right.svg)
# 文件树的类型图标（default_file_icons.json 的查表目标）。
file(GLOB _tst_toolsui_filetype_icons
    "${CMAKE_SOURCE_DIR}/icons/filetypes/*.svg")
file(GLOB _tst_toolsui_foldertype_icons
    "${CMAKE_SOURCE_DIR}/icons/foldertypes/*.svg")
foreach(_icon IN LISTS _tst_toolsui_icons)
    get_filename_component(_name ${_icon} NAME)
    set_source_files_properties(${_icon}
        PROPERTIES QT_RESOURCE_ALIAS "icons/${_name}")
endforeach()
foreach(_icon IN LISTS _tst_toolsui_filetype_icons)
    get_filename_component(_name ${_icon} NAME)
    set_source_files_properties(${_icon}
        PROPERTIES QT_RESOURCE_ALIAS "icons/filetypes/${_name}")
endforeach()
foreach(_icon IN LISTS _tst_toolsui_foldertype_icons)
    get_filename_component(_name ${_icon} NAME)
    set_source_files_properties(${_icon}
        PROPERTIES QT_RESOURCE_ALIAS "icons/foldertypes/${_name}")
endforeach()

awb_add_resources(tst_toolsui "test_resources"
    PREFIX "/"
    FILES
        ${_tst_toolsui_default_icons}
        ${_tst_toolsui_icons}
        ${_tst_toolsui_filetype_icons}
        ${_tst_toolsui_foldertype_icons}
)

# 页面 + 它引用的组件。QT_RESOURCE_ALIAS 决定 qrc URL，与 app 一致。
set(_tst_toolsui_qml
    ${CMAKE_SOURCE_DIR}/src/tools/qml/ToolsPage.qml
    ${CMAKE_SOURCE_DIR}/src/tools/qml/MarkdownContextMenu.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/PageHeader.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AIconButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextArea.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AEmptyState.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AScrollBar.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenu.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuItem.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuSeparator.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AComboBox.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/ToolsPage.qml
    PROPERTIES QT_RESOURCE_ALIAS "tools/ToolsPage.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/MarkdownContextMenu.qml
    PROPERTIES QT_RESOURCE_ALIAS "tools/MarkdownContextMenu.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/shell/qml/PageHeader.qml
    PROPERTIES QT_RESOURCE_ALIAS "shell/PageHeader.qml")
foreach(_comp AButton AIconButton ATextArea AEmptyState AScrollBar
             AMenu AMenuItem AMenuSeparator AComboBox)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/${_comp}.qml
        PROPERTIES QT_RESOURCE_ALIAS "components/${_comp}.qml")
endforeach()

awb_add_qml_module(tst_toolsui
    URI AgentWorkbench
    VERSION 1.0
    QML_FILES ${_tst_toolsui_qml}
)

add_test(NAME tst_toolsui COMMAND tst_toolsui)
