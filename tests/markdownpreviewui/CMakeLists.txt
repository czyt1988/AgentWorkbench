# tst_markdownpreviewui — 预览窗口（MarkdownPreviewWindow.qml）的加载与往返冒烟。
#
# 与 tst_markdownpreview 的分工：那边把内联 QML 造的裸视图喂给页面运行时（页面层），
# 这边加载产品那份窗口 QML（窗口层）——组件引用、C++ 单例名、命令泵、导出落盘
# 都只有走这份文件才谈得上验证。嵌进去的 QML/图标/运行时资源必须与
# app/CMakeLists.txt 的别名一致，否则测的就不是产品跑起来的那份。
#
# 只在 Qt 6 + WebEngine 的连接里存在（见 tests/CMakeLists.txt）；窗口那份 .qml
# 也只在同一条件下登记进 app 的 QML 模块，两边守卫同源。
add_executable(tst_markdownpreviewui
    tst_markdownpreviewui.cpp
)

target_include_directories(tst_markdownpreviewui PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(tst_markdownpreviewui PRIVATE
    awb_markdown
    awb_markdown_webengine
    awb_core
    awb_theme
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
awb_add_resources(tst_markdownpreviewui "previewui_icons"
    PREFIX "/"
    FILES
        ${_tst_markdownpreviewui_icons}
)

# 窗口 + 它引用的共享组件（QT_RESOURCE_ALIAS 与 app 一致，URL 才能对上）。
# 新引用一个组件就把它加进下面两处：漏嵌时本目标以「unknown type」失败，
# 而那正是产品在运行时打不开预览窗的原因。
set(_tst_markdownpreviewui_qml
    ${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownPreviewWindow.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AIconButton.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASearchField.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextField.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextArea.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AScrollBar.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ADialog.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AConfirmDialog.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASpotlight.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenu.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuItem.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuSeparator.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AComboBox.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownPreviewWindow.qml
    PROPERTIES QT_RESOURCE_ALIAS "markdown/MarkdownPreviewWindow.qml")
foreach(_comp AButton AIconButton ASearchField ATextField ATextArea AScrollBar
             ADialog AConfirmDialog ASpotlight AMenu AMenuItem AMenuSeparator
             AComboBox)
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
