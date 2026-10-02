# tst_markdownpreview — 预览页面在真实引擎里的渲染冒烟（Qt 6 + WebEngine 专属）。
#
# 与 tst_webengine 的分工：那边证明 awbmd:// 供得出脚本、字体与信标（供体层），
# 这边证明 shell.html + bridge.js + renderer.js 这条管线把文档渲染成了什么
# （页面层）。所以本目标把 app 用的那棵树原样嵌进来：vendor 资产与 runtime/*
# 的前缀、别名必须与 app/CMakeLists.txt 一致——不一致时页面里 /app/bridge.js
# 取到的就不是产品跑起来取到的那份，冒烟就测了个寂寞。
add_executable(tst_markdownpreview
    tst_markdownpreview.cpp
)

target_include_directories(tst_markdownpreview PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(tst_markdownpreview PRIVATE
    awb_markdown
    awb_markdown_webengine
    awb_core
    awb_theme
    Qt::Core
    Qt::Gui
    Qt::Qml
    Qt::Test
)

awb_add_vendor_resources(tst_markdownpreview "preview_markdown_vendor"
    PREFIX "/markdown"
    ALIAS_PREFIX "vendor"
    ROOT ${CMAKE_SOURCE_DIR}/third_party/markdown-runtime
    EXCLUDE "README\\.md$" "/LICENSE$"
)

# 页面运行时：与 app/CMakeLists.txt 同一棵树、同一个 app/ 别名前缀。
awb_add_vendor_resources(tst_markdownpreview "preview_markdown_page_runtime"
    PREFIX "/markdown"
    ALIAS_PREFIX "app"
    ROOT ${CMAKE_SOURCE_DIR}/src/markdown/webengine/runtime
)

# 主题：用例里 theme.applyTheme("mocha-dark") 要读得到随包主题 JSON，
# CSS 变量才进得了 doc.json 与导出件（嵌法同 tst_webengine）。
set(_tst_markdownpreview_themes
    ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json)
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
    PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")
awb_add_resources(tst_markdownpreview "preview_themes"
    PREFIX "/themes"
    FILES
        ${_tst_markdownpreview_themes}
)

add_test(NAME tst_markdownpreview COMMAND tst_markdownpreview)
