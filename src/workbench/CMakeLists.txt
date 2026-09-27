# awb_workbench (L3): composition root intents — the only module allowed to
# depend on every domain.
qt_add_library(awb_workbench STATIC
    BuiltinPages.cpp
    BuiltinPages.h
    EnvironmentService.cpp
    EnvironmentService.h
    PluginServices.cpp
    PluginServices.h
    WorkbenchContext.cpp
    WorkbenchContext.h
)

target_include_directories(awb_workbench PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_workbench
    PUBLIC
        awb_agentcatalog
        awb_core
        awb_shell
        awb_skillcatalog
        awb_theme
        awb_tools
        awb_web
        Qt6::Core
        Qt6::Gui
)
