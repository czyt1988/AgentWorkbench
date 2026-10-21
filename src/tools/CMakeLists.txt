# awb_tools (L2 domain): the Agent Tools page — workspace memory, lazy file
# tree, prompt draft and template library. Depends on awb_core and awb_theme —
# never on shell or other domains. (The markdown editing engine lives in
# awb_markdownedit.)
add_library(awb_tools STATIC
    FileIcons.cpp
    FileIcons.h
    FileTreeFlatModel.cpp
    FileTreeFlatModel.h
    FileTreeModel.cpp
    FileTreeModel.h
    PromptChecks.cpp
    PromptChecks.h
    PromptTemplate.cpp
    PromptTemplate.h
    PromptTemplateLibrary.cpp
    PromptTemplateLibrary.h
    ToolsFacade.cpp
    ToolsFacade.h
    ToolsStore.cpp
    ToolsStore.h
)

target_include_directories(awb_tools PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_tools
    PUBLIC
        awb_core
        awb_theme
        Qt::Core
        Qt::Gui
)
