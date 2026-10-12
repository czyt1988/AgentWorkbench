# awb_markdown_webengine (L2 adapter): the markdown domain's only target that
# links Qt WebEngine — the awbmd:// scheme handler and the preview profile that
# serves it.
#
# The same invariant as src/web/webengine applies to the domain as a whole:
# only *_webengine adapter targets link WebEngine, so a build with
# AWB_ENABLE_WEBENGINE=OFF still compiles and runs (it just has no preview —
# see MarkdownRuntime::isSupportedBuild()).
#
# Both Qt majors: the C++ surface (QWebEngineUrlScheme, QQuickWebEngineProfile,
# QWebEngineUrlRequestJob) is present in Qt 5.15.16 with the same shapes. The
# one version branch lives in MarkdownPreviewProvider.cpp (FetchApiAllowed
# exists from Qt 6.6 only; older engines allow fetch on custom schemes through
# CorsEnabled alone).
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
