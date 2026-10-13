# tst_workbench — the cross-domain hub (WorkbenchContext):
# openWeb's tab creation + navigation, openWebExternal's browser path.
add_executable(tst_workbench
    tst_workbench.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_workbench PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# The bundled defaults are an absolute path outside this directory; the
# alias must be declared before awb_add_resources() processes the file
# (the repository seeds the data root from the qrc copy).
set(_tst_workbench_default_config ${CMAKE_SOURCE_DIR}/config/default_agents.json)
set_source_files_properties(${_tst_workbench_default_config}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_agents.json")

awb_add_resources(tst_workbench "test_resources"
    PREFIX "/"
    FILES
        ${_tst_workbench_default_config}
)

target_link_libraries(tst_workbench PRIVATE
    awb_workbench
    awb_agentcatalog
    awb_web
    awb_shell
    awb_core
    awb_theme
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Test
)

add_test(NAME tst_workbench COMMAND tst_workbench)

# tst_ribbonui — SARibbon 命令条的运行期冒烟，只在 AWB_ENABLE_RIBBON 的
# 构建里存在（与被测的 WorkbenchRibbon.qml 同一道开关；其余 QML 测试刻意
# 不链 SARibbon，装载的都是降级工具栏）。自有 main：Quick 场景需要
# QGuiApplication。它压的是静态链接 SARibbon 的三个风险面：qrc 初始化、
# 命令式注册的时机、叶子创建——细节见 tst_ribbonui.cpp 的头注释。
if (AWB_ENABLE_RIBBON)
    add_executable(tst_ribbonui
        tst_ribbonui.cpp
    )

    target_include_directories(tst_ribbonui PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # seedThemes() 要在 ThemeRegistry 构造前从源码树把内置主题 JSON 拷进测试
    # 数据目录，靠的是 QT_TESTCASE_SOURCEDIR。Qt 6 的测试体系自带这个定义，
    # Qt 5 的 Qt5::Test 只给 QT_TESTCASE_BUILDDIR（见 Qt5TestConfigExtras.cmake），
    # 缺了它在 Qt 5 上整个文件编不过，这里按同名语义补上。
    if (QT_VERSION_MAJOR EQUAL 5)
        target_compile_definitions(tst_ribbonui PRIVATE
            QT_TESTCASE_SOURCEDIR="${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    target_link_libraries(tst_ribbonui PRIVATE
        SARibbon::Qml
        awb_tools
        awb_markdown
        awb_theme
        awb_core
        Qt::Core
        Qt::Gui
        Qt::Qml
        Qt::Quick
        Qt::QuickControls2
        Qt::Test
    )

    # 与其它 QML 测试同款：主题 JSON、文件树图标表与命令条用到的图标都按
    # app 的 :/ URL 嵌进来，测的才是产品那份资源（alias 先于
    # awb_add_resources 声明）。
    set(_tst_ribbonui_themes
        ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
        ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
        PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
        PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")
    set(_tst_ribbonui_default_icons
        ${CMAKE_SOURCE_DIR}/config/default_file_icons.json)
    set_source_files_properties(${_tst_ribbonui_default_icons}
        PROPERTIES QT_RESOURCE_ALIAS "config/default_file_icons.json")
    # 内置模板表：第三个用例装载整页，模板段要真有行可数（tst_toolsui
    # 同款——ToolsFacade 的内置模板从这份 qrc 读）。
    set(_tst_ribbonui_default_templates
        ${CMAKE_SOURCE_DIR}/config/default_prompt_templates.json)
    set_source_files_properties(${_tst_ribbonui_default_templates}
        PROPERTIES QT_RESOURCE_ALIAS "config/default_prompt_templates.json")
    set(_tst_ribbonui_icons
        ${CMAKE_SOURCE_DIR}/icons/copy.svg
        ${CMAKE_SOURCE_DIR}/icons/paste.svg
        ${CMAKE_SOURCE_DIR}/icons/cut.svg
        ${CMAKE_SOURCE_DIR}/icons/undo.svg
        ${CMAKE_SOURCE_DIR}/icons/redo.svg
        ${CMAKE_SOURCE_DIR}/icons/bold.svg
        ${CMAKE_SOURCE_DIR}/icons/code.svg
        ${CMAKE_SOURCE_DIR}/icons/bullet-list.svg
        ${CMAKE_SOURCE_DIR}/icons/search.svg
        ${CMAKE_SOURCE_DIR}/icons/download.svg
        ${CMAKE_SOURCE_DIR}/icons/web.svg
        ${CMAKE_SOURCE_DIR}/icons/menu.svg
        ${CMAKE_SOURCE_DIR}/icons/close.svg
        ${CMAKE_SOURCE_DIR}/icons/plus.svg
        ${CMAKE_SOURCE_DIR}/icons/file.svg
        ${CMAKE_SOURCE_DIR}/icons/folder.svg
        ${CMAKE_SOURCE_DIR}/icons/refresh.svg
        ${CMAKE_SOURCE_DIR}/icons/chevron-left.svg
        ${CMAKE_SOURCE_DIR}/icons/chevron-right.svg
        ${CMAKE_SOURCE_DIR}/icons/external-link.svg)
    foreach(_icon IN LISTS _tst_ribbonui_icons)
        get_filename_component(_name ${_icon} NAME)
        set_source_files_properties(${_icon}
            PROPERTIES QT_RESOURCE_ALIAS "icons/${_name}")
    endforeach()
    # 命令条图标库（data-URL SVG 染色）：与 app 相同的资源路线、落在 QML
    # 模块的 URL 空间里（ribbon 的相对 import 才解析得到）。
    set(_tst_ribbonui_ribbon_icons
        ${CMAKE_SOURCE_DIR}/src/workbench/qml/RibbonIcons.js)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/workbench/qml/RibbonIcons.js
        PROPERTIES QT_RESOURCE_ALIAS
        "qt/qml/AgentWorkbench/workbench/RibbonIcons.js")
    awb_add_resources(tst_ribbonui "ribbonui_resources"
        PREFIX "/"
        FILES
            ${_tst_ribbonui_themes}
            ${_tst_ribbonui_default_icons}
            ${_tst_ribbonui_default_templates}
            ${_tst_ribbonui_icons}
            ${_tst_ribbonui_ribbon_icons}
    )

    # 被测的那份 QML 按与 app 相同的 URL/别名嵌入。第三个用例装载整份
    # MarkdownWorkbenchPage.qml（清单与 tst_toolsui 同款：页面、降级工具
    # 栏、右键菜单与全部 A* 组件）——「页面 + SARibbon」的组合只有本目标
    # 覆盖（其余 QML 测试刻意不链 SARibbon，页面在它们那里走降级工具栏），
    # 首显 category、delegate 作用域与 ObjectModel 用法都压在这里。
    set(_tst_ribbonui_qml
        ${CMAKE_SOURCE_DIR}/src/workbench/qml/WorkbenchRibbon.qml
        ${CMAKE_SOURCE_DIR}/src/workbench/qml/MarkdownWorkbenchPage.qml
        ${CMAKE_SOURCE_DIR}/src/workbench/qml/WorkbenchToolbar.qml
        ${CMAKE_SOURCE_DIR}/src/tools/qml/MarkdownContextMenu.qml
        ${CMAKE_SOURCE_DIR}/src/tools/qml/PromptChecksPopup.qml
        ${CMAKE_SOURCE_DIR}/src/tools/qml/DraftHistoryPopup.qml
        ${CMAKE_SOURCE_DIR}/src/tools/qml/WorkspaceTreePanel.qml
        ${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownOutlinePanel.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AButton.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AIconButton.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextArea.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ATextField.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AEmptyState.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AScrollBar.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASearchField.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/APill.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ADialog.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AConfirmDialog.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASectionHeader.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AListRow.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenu.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuItem.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AMenuSeparator.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AComboBox.qml
        ${CMAKE_SOURCE_DIR}/src/shell/qml/components/ASplitHandle.qml)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/workbench/qml/WorkbenchRibbon.qml
        PROPERTIES QT_RESOURCE_ALIAS "workbench/WorkbenchRibbon.qml")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/workbench/qml/MarkdownWorkbenchPage.qml
        PROPERTIES QT_RESOURCE_ALIAS "workbench/MarkdownWorkbenchPage.qml")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/workbench/qml/WorkbenchToolbar.qml
        PROPERTIES QT_RESOURCE_ALIAS "workbench/WorkbenchToolbar.qml")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/MarkdownContextMenu.qml
        PROPERTIES QT_RESOURCE_ALIAS "tools/MarkdownContextMenu.qml")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/PromptChecksPopup.qml
        PROPERTIES QT_RESOURCE_ALIAS "tools/PromptChecksPopup.qml")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/DraftHistoryPopup.qml
        PROPERTIES QT_RESOURCE_ALIAS "tools/DraftHistoryPopup.qml")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/tools/qml/WorkspaceTreePanel.qml
        PROPERTIES QT_RESOURCE_ALIAS "tools/WorkspaceTreePanel.qml")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/markdown/qml/MarkdownOutlinePanel.qml
        PROPERTIES QT_RESOURCE_ALIAS "markdown/MarkdownOutlinePanel.qml")
    foreach(_comp AButton AIconButton ATextArea ATextField AEmptyState AScrollBar
                 ASearchField APill ADialog AConfirmDialog ASectionHeader AListRow
                 AMenu AMenuItem AMenuSeparator AComboBox ASplitHandle)
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/src/shell/qml/components/${_comp}.qml
            PROPERTIES QT_RESOURCE_ALIAS "components/${_comp}.qml")
    endforeach()

    awb_add_qml_module(tst_ribbonui
        URI AgentWorkbench
        VERSION 1.0
        QML_FILES ${_tst_ribbonui_qml}
    )

    add_test(NAME tst_ribbonui COMMAND tst_ribbonui)
endif()
