# awb_tools (L2 domain): the Agent Tools page — workspace memory, lazy file
# tree, prompt draft. Depends on awb_core only — never on shell or other domains.
qt_add_library(awb_tools STATIC
    FileTreeModel.cpp
    FileTreeModel.h
    ToolsFacade.cpp
    ToolsFacade.h
    ToolsStore.cpp
    ToolsStore.h
)

target_include_directories(awb_tools PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_tools
    PUBLIC
        awb_core
        Qt6::Core
)
