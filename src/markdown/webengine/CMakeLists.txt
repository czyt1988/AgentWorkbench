# awb_markdown_webengine (L2 adapter): the markdown domain's only target that
# links Qt WebEngine — the awbmd:// scheme handler and the preview profile that
# serves it.
#
# The same invariant as src/web/webengine applies to the domain as a whole:
# only *_webengine adapter targets link WebEngine, so a build with
# AWB_ENABLE_WEBENGINE=OFF still compiles and runs (it just has no preview —
# see MarkdownRuntime::isSupportedBuild()).
#
# Qt 6 only: src/markdown/CMakeLists.txt adds this directory under
# `AWB_ENABLE_WEBENGINE AND QT_VERSION_MAJOR EQUAL 6`, because the Qt 5 route
# embeds neither the page runtime nor the vendor assets — a Qt 5 binary does
# not contain a reduced version of the feature, it does not contain it at all.
add_library(awb_markdown_webengine STATIC
    MarkdownPreviewProvider.cpp
    MarkdownPreviewProvider.h
    MarkdownRuntimeHandler.cpp
    MarkdownRuntimeHandler.h
)

target_include_directories(awb_markdown_webengine PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_markdown_webengine
    PUBLIC
        awb_markdown
        Qt::Core
        Qt::Gui
        ${AWB_WEBENGINE_TARGET}
)
