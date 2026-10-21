# awb_markdownedit (L2 domain): the markdown editing engine — formatting
# actions over QTextCursor, find/replace, and the syntax highlighter. A
# self-contained, reusable module: depends only on awb_theme (highlighter
# colors come from theme tokens) — never on core, shell or other domains.
# Qt Quick is a PRIVATE link: only MarkdownEdit touches QQuickTextDocument
# to read a TextArea's document; dependents stay UI-free.
add_library(awb_markdownedit STATIC
    MarkdownEdit.cpp
    MarkdownEdit.h
    MarkdownHighlighter.cpp
    MarkdownHighlighter.h
)

target_include_directories(awb_markdownedit PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_markdownedit
    PUBLIC
        awb_theme
        Qt::Core
        Qt::Gui
    PRIVATE
        Qt::Quick
)
