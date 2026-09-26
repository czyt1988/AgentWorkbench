# awb_workbench (L3): composition root intents — the only module allowed to
# depend on every domain (specs/01-architecture.md §4.8).
qt_add_library(awb_workbench STATIC
    BuiltinPages.cpp
    BuiltinPages.h
    EnvironmentService.cpp
    EnvironmentService.h
    WorkbenchContext.cpp
    WorkbenchContext.h
)

target_include_directories(awb_workbench PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_workbench
    PUBLIC
        awb_agents
        awb_core
        awb_shell
        awb_theme
        awb_web
        Qt6::Core
        Qt6::Gui
)
