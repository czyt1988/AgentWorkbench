# awb_web_webengine (L2 adapter): the only target linking Qt WebEngine.
# The program still builds with
# AWB_ENABLE_WEBENGINE=OFF — the external surface takes over.
qt_add_library(awb_web_webengine STATIC
    WebEngineProfileStore.cpp
    WebEngineProfileStore.h
    WebEngineSurfaceProvider.cpp
    WebEngineSurfaceProvider.h
)

target_include_directories(awb_web_webengine PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_web_webengine
    PUBLIC
        awb_web
        Qt6::Core
        Qt6::WebEngineQuick
)
