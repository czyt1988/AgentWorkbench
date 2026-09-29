# awb_tools (L2 domain): the Agent Tools page — workspace memory, lazy file
# tree, prompt draft, markdown editing support. Depends on awb_core and
# awb_theme (highlighter colors come from theme tokens) — never on shell or
# other domains. Qt Quick is a PRIVATE link: only MarkdownEdit touches
# QQuickTextDocument to read a TextArea's document; dependents stay UI-free.
add_library(awb_tools STATIC
    FileIcons.cpp
    FileIcons.h
    FileTreeFlatModel.cpp
    FileTreeFlatModel.h
    FileTreeModel.cpp
    FileTreeModel.h
    MarkdownEdit.cpp
    MarkdownEdit.h
    MarkdownHighlighter.cpp
    MarkdownHighlighter.h
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
    PRIVATE
        Qt::Quick
)
