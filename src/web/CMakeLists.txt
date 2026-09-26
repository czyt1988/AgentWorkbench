# awb_web (L2 domain): tab model, surface selection, memory policy.
# Links awb_core/awb_theme only — no WebEngine here; the heavy adapter is
# the sibling webengine/ target (specs/01-architecture.md §4.5/§4.6).
qt_add_library(awb_web STATIC
    WebProfilePaths.cpp
    WebProfilePaths.h
    WebSurfaceRegistry.cpp
    WebSurfaceRegistry.h
    WebTab.cpp
    WebTab.h
    WebTabsFacade.cpp
    WebTabsFacade.h
    WebTabsModel.cpp
    WebTabsModel.h
)

target_include_directories(awb_web PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_web
    PUBLIC
        awb_core
        awb_theme
        Qt6::Core
        Qt6::Gui
        Qt6::Network
)

# The WebEngine adapter (specs/01 §4.6) — the only target allowed to link
# Qt WebEngine; the program still builds and runs without it.
if (AWB_ENABLE_WEBENGINE)
    add_subdirectory(webengine)
endif()
