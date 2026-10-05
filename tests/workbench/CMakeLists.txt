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
        ${CMAKE_SOURCE_DIR}/icons/menu.svg)
    foreach(_icon IN LISTS _tst_ribbonui_icons)
        get_filename_component(_name ${_icon} NAME)
        set_source_files_properties(${_icon}
            PROPERTIES QT_RESOURCE_ALIAS "icons/${_name}")
    endforeach()
    awb_add_resources(tst_ribbonui "ribbonui_resources"
        PREFIX "/"
        FILES
            ${_tst_ribbonui_themes}
            ${_tst_ribbonui_default_icons}
            ${_tst_ribbonui_icons}
    )

    # 被测的那份 QML 按与 app 相同的 URL/别名嵌入。
    set(_tst_ribbonui_qml
        ${CMAKE_SOURCE_DIR}/src/workbench/qml/WorkbenchRibbon.qml)
    set_source_files_properties(${_tst_ribbonui_qml}
        PROPERTIES QT_RESOURCE_ALIAS "workbench/WorkbenchRibbon.qml")

    awb_add_qml_module(tst_ribbonui
        URI AgentWorkbench
        VERSION 1.0
        QML_FILES ${_tst_ribbonui_qml}
    )

    add_test(NAME tst_ribbonui COMMAND tst_ribbonui)
endif()
