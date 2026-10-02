# awb_markdown (L2 domain): the Markdown preview window's state layer —
# document I/O with byte-fidelity save, block chunking, the awbmd:// URL/MIME/
# theme-string layer, and the controller behind the `Markdown` QML singleton.
# Links awb_core/awb_theme only; no WebEngine and no QML here, so the whole
# domain builds and tests in a WebEngine-OFF binary (the window itself is
# simply not offered there — see MarkdownRuntime::isSupportedBuild()).
add_library(awb_markdown STATIC
    MarkdownChunker.cpp
    MarkdownChunker.h
    MarkdownDocument.cpp
    MarkdownDocument.h
    MarkdownPreviewController.cpp
    MarkdownPreviewController.h
    MarkdownRuntime.cpp
    MarkdownRuntime.h
)

target_include_directories(awb_markdown PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_markdown
    PUBLIC
        awb_core
        awb_theme
        Qt::Core
        Qt::Gui
)

# The single runtime gate for "this binary can preview Markdown". app/ uses the
# identical condition to embed third_party/markdown-runtime under :/markdown/,
# so a build either has both the assets and the capability, or neither — the
# failure mode of letting the two drift apart is a menu item that opens a 404.
if (AWB_ENABLE_WEBENGINE AND QT_VERSION_MAJOR EQUAL 6)
    target_compile_definitions(awb_markdown
        PUBLIC AWB_MD_PREVIEW_SUPPORTED=1)
endif()

# The WebEngine adapter (awbmd:// scheme handler + the preview profile) is the
# only target in this domain allowed to link Qt WebEngine; it lands with the
# same condition as the define above. Qt 5 gets neither the adapter nor the
# assets — see src/markdown/webengine/CMakeLists.txt.
if (AWB_ENABLE_WEBENGINE AND QT_VERSION_MAJOR EQUAL 6)
    add_subdirectory(webengine)
endif()
