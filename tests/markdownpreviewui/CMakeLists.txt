# tst_markdownpreviewui — 预览窗口（MarkdownPreviewWindow.qml）的加载与往返冒烟。
#
# 与 tst_markdownpreview 的分工：那边把内联 QML 造的裸视图喂给页面运行时（页面层），
# 这边加载产品那份窗口 QML（窗口层）——组件引用、C++ 单例名、命令泵、导出落盘
# 都只有走这份文件才谈得上验证。嵌进去的 QML/图标/运行时资源必须与
# app/CMakeLists.txt 的别名一致，否则测的就不是产品跑起来的那份。
#
# 只在 WebEngine 的连接里存在（见 tests/CMakeLists.txt，Qt 5 / Qt 6 都跑）；
# 预览面那份 .qml 也登记在同一条件下，两边守卫同源。awb_web_webengine 提供
# 预览面导航应答用的 WebEngineCompat 单例真身。
add_executable(tst_markdownpreviewui
    tst_markdownpreviewui.cpp
)

target_include_directories(tst_markdownpreviewui PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(tst_markdownpreviewui PRIVATE
    awb_markdown
    awb_markdown_webengine
    awb_web_webengine
    awb_tools
    awb_core
    awb_theme
    awb_web
    Qt::Core
    Qt::Gui
    Qt::Qml
    Qt::Quick
    Qt::QuickControls2
    Qt::Test
)

# 页面运行时与 vendor 资产：与 app 同一棵树、同两个别名前缀（app/ 与 vendor/），
# awbmd:// 的 handler 按这两处解析，少一个就是页面 404。
awb_add_vendor_resources(tst_markdownpreviewui "previewui_markdown_vendor"
    PREFIX "/markdown"
    ALIAS_PREFIX "vendor"
    ROOT ${CMAKE_SOURCE_DIR}/third_party/markdown-runtime
    EXCLUDE "README\\.md$" "/LICENSE$"
)
awb_add_vendor_resources(tst_markdownpreviewui "previewui_markdown_page_runtime"
    PREFIX "/markdown"
    ALIAS_PREFIX "app"
    ROOT ${CMAKE_SOURCE_DIR}/src/markdown/webengine/runtime
)

# 内置主题：CSS 变量与界面令牌都要读得到随包主题 JSON。
set(_tst_markdownpreviewui_themes
    ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json)
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
    PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")
awb_add_resources(tst_markdownpreviewui "previewui_themes"
    PREFIX "/themes"
    FILES
        ${_tst_markdownpreviewui_themes}
)

# 图标：窗口与它引用的组件用到的全部顶层 SVG（AMenuItem 的图标槽、AIconButton
# 的 iconSource）。整目录收，省掉「新加一个图标就少嵌一个」的长期维护。
file(GLOB _tst_markdownpreviewui_icons "${CMAKE_SOURCE_DIR}/icons/*.svg")
foreach(_icon IN LISTS _tst_markdownpreviewui_icons)
    get_filename_component(_name ${_icon} NAME)
    set_source_files_properties(${_icon}
        PROPERTIES QT_RESOURCE_ALIAS "icons/${_name}")
endforeach()
# 工作区文件树的类型图标表（窗口带着左侧树，FileIcons 查表用的数据与
# 图标都要在场，与 tst_toolsui 同款）。
set(_tst_markdownpreviewui_default_icons
    ${CMAKE_SOURCE_DIR}/config/default_file_icons.json)
set_source_files_properties(${_tst_markdownpreviewui_default_icons}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_file_icons.json")
# 内置模板表（窗口的模板库段从 :/config/ 读）。
set(_tst_markdownpreviewui_templates
    ${CMAKE_SOURCE_DIR}/config/default_prompt_templates.json)
set_source_files_properties(${_tst_markdownpreviewui_templates}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_prompt_templates.json")
file(GLOB _tst_markdownpreviewui_filetype_icons
    "${CMAKE_SOURCE_DIR}/icons/filetypes/*.svg")
file(GLOB _tst_markdownpreviewui_foldertype_icons
    "${CMAKE_SOURCE_DIR}/icons/foldertypes/*.svg")
foreach(_icon IN LISTS _tst_markdownpreviewui_filetype_icons)
    get_filename_component(_name ${_icon} NAME)
    set_source_files_properties(${_icon}
        PROPERTIES QT_RESOURCE_ALIAS "icons/filetypes/${_name}")
endforeach()
foreach(_icon IN LISTS _tst_markdownpreviewui_foldertype_icons)
    get_filename_component(_name ${_icon} NAME)
    set_source_files_properties(${_icon}
        PROPERTIES QT_RESOURCE_ALIAS "icons/foldertypes/${_name}")
endforeach()
awb_add_resources(tst_markdownpreviewui "previewui_icons"
    PREFIX "/"
    FILES
        ${_tst_markdownpreviewui_icons}
        ${_tst_markdownpreviewui_default_icons}
        ${_tst_markdownpreviewui_templates}
        ${_tst_markdownpreviewui_filetype_icons}
        ${_tst_markdownpreviewui_foldertype_icons}
)

# 窗口 + 预览面 + 降级命令条 + 它们引用的共享组件（QT_RESOURCE_ALIAS 与
# app 一致，URL 才能对上）。命令条只嵌工具栏形态——本目标不链接 SARibbon，
# 窗口里那个 Loader 装载 Ribbon 失败后换装载它，与产品降级路径同一条。
# 新引用一个组件就把它加进下面两处：漏嵌时本目标以「unknown type」失败，
# 而那正是产品在运行时打不开窗口的原因。
set(_tst_markdownpreviewui_qml
    ${CMAKE_SOURCE_DIR}/src/workbench/qml/MarkdownWorkbenchPage.qml
    ${CMAKE_SOURCE_DIR}/src/workbench/qml/WorkbenchToolbar.qml
    ${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownPreviewPane.qml
    ${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownOutlinePanel.qml
    ${CMAKE_SOURCE_DIR}/src/tools/qml/MarkdownContextMenu.qml
    ${CMAKE_SOURCE_DIR}/src/tools/qml/PromptChecksPopup.qml
    ${CMAKE_SOURCE_DIR}/src/tools/qml/DraftHistoryPopup.qml
    ${CMAKE_SOURCE_DIR}/src/tools/qml/WorkspaceTreePanel.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AIconButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASearchField.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextField.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextArea.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AScrollBar.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AEmptyState.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/APill.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ADialog.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AConfirmDialog.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASpotlight.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASectionHeader.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AListRow.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenu.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuItem.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuSeparator.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AComboBox.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASplitHandle.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/workbench/qml/MarkdownWorkbenchPage.qml
    PROPERTIES QT_RESOURCE_ALIAS "workbench/MarkdownWorkbenchPage.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/workbench/qml/WorkbenchToolbar.qml
    PROPERTIES QT_RESOURCE_ALIAS "workbench/WorkbenchToolbar.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownPreviewPane.qml
    PROPERTIES QT_RESOURCE_ALIAS "markdown/MarkdownPreviewPane.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownOutlinePanel.qml
    PROPERTIES QT_RESOURCE_ALIAS "markdown/MarkdownOutlinePanel.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/MarkdownContextMenu.qml
    PROPERTIES QT_RESOURCE_ALIAS "tools/MarkdownContextMenu.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/PromptChecksPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS "tools/PromptChecksPopup.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/DraftHistoryPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS "tools/DraftHistoryPopup.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/WorkspaceTreePanel.qml
    PROPERTIES QT_RESOURCE_ALIAS "tools/WorkspaceTreePanel.qml")
foreach(_comp AButton AIconButton ASearchField ATextField ATextArea AScrollBar
             AEmptyState APill ADialog AConfirmDialog ASpotlight ASectionHeader
             AListRow AMenu AMenuItem AMenuSeparator AComboBox ASplitHandle)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/${_comp}.qml
        PROPERTIES QT_RESOURCE_ALIAS "components/${_comp}.qml")
endforeach()

awb_add_qml_module(tst_markdownpreviewui
    URI AgentWorkbench
    VERSION 1.0
    QML_FILES ${_tst_markdownpreviewui_qml}
)

add_test(NAME tst_markdownpreviewui COMMAND tst_markdownpreviewui)
