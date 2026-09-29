# awb_web_webengine (L2 adapter): the only target linking Qt WebEngine.
# The program still builds with
# AWB_ENABLE_WEBENGINE=OFF — the external surface takes over.
add_library(awb_web_webengine STATIC
    WebEngineCompat.cpp
    WebEngineCompat.h
    WebEngineProfileStore.cpp
    WebEngineProfileStore.h
    WebEngineSurfaceProvider.cpp
    WebEngineSurfaceProvider.h
)

target_include_directories(awb_web_webengine PUBLIC ${CMAKE_SOURCE_DIR}/src)

# QQuickWebEngineView itself is private API on BOTH majors (the public
# include dir only ships Profile/Script/DownloadRequest); WebEngineCompat
# needs it for the version bridging (signals, request types,
# grantFeaturePermission, setInspectedView), so the private include dirs
# are added per version.
if (QT_VERSION_MAJOR EQUAL 5)
    target_include_directories(awb_web_webengine
        PRIVATE ${Qt5WebEngine_PRIVATE_INCLUDE_DIRS})
elseif (QT_VERSION_MAJOR EQUAL 6)
    target_include_directories(awb_web_webengine
        PRIVATE ${Qt6WebEngineQuick_PRIVATE_INCLUDE_DIRS})
endif()

target_link_libraries(awb_web_webengine
    PUBLIC
        awb_web
        Qt::Core
        ${AWB_WEBENGINE_TARGET}
)
