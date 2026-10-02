# tst_markdown — Markdown 预览的纯层：切分器、文档读写、awbmd:// 字符串层、
# 预览控制器。领域库本身不碰 WebEngine，所以这个测试在 Qt 5 与
# AWB_ENABLE_WEBENGINE=OFF 的构建里也一样跑（能力开关相关的用例自己跳过）。
add_executable(tst_markdown
    tst_markdown_main.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
    tst_markdownchunker.cpp
    tst_markdowndocument.cpp
    tst_markdownruntime.cpp
    tst_markdownpreviewcontroller.cpp
)

target_include_directories(tst_markdown PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src
)

# 切分保真性的语料根：仓库自己的 docs/。测试只读不写，路径在配置期定死，
# 所以用例跑起来不依赖当前工作目录。
target_compile_definitions(tst_markdown PRIVATE
    AWB_DOCS_DIR="${CMAKE_SOURCE_DIR}/docs")

# vendor 资产按 app/CMakeLists.txt 的同一套前缀与别名再嵌一遍。两个理由：
# 一是清单核对（每个 sha256、每个 URL）必须读得到真实字节；二是「代码算出来的
# awbmd:// URL 与嵌入资源的 qrc 路径是同一个」这件事只有在嵌了才测得了。
# 这里**不加** AWB_ENABLE_WEBENGINE 条件：资产在仓库里，完整性在任何构建里
# 都该成立——被关掉的只是可执行文件里带不带它。
awb_add_vendor_resources(tst_markdown "markdown_vendor"
    PREFIX "/markdown"
    ALIAS_PREFIX "vendor"
    ROOT ${CMAKE_SOURCE_DIR}/third_party/markdown-runtime
    EXCLUDE "README\\.md$" "/LICENSE$"
)

# 页面自己的运行时（runtime/*.js、shell.html、theme.css）也按 app/ 别名嵌一份。
# 不是为了在这里跑页面——JS 引擎在本目标里不存在——而是因为 bridge.js 与 C++
# 的命令表/事件表是字符串协议：把源码读进来比对分支名，就能在纯层测试里挡住
# 「一边改名一边没改」，不必等到 WebEngine 冒烟（那个目标在关掉的构建里根本
# 不建）。同 app/CMakeLists.txt 的前缀与别名，两边必须是同一棵树。
awb_add_vendor_resources(tst_markdown "markdown_page_runtime"
    PREFIX "/markdown"
    ALIAS_PREFIX "app"
    ROOT ${CMAKE_SOURCE_DIR}/src/markdown/webengine/runtime
)

target_link_libraries(tst_markdown PRIVATE
    awb_markdown
    awb_core
    awb_theme
    Qt::Core
    Qt::Gui
    Qt::Test
)

add_test(NAME tst_markdown COMMAND tst_markdown)
