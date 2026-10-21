# tst_markdownedit — the markdown editing engine: formatting actions over
# QTextCursor (bold/italic/code/lists/headings/find/replace/Tab), and the
# highlighter's block state machine. Pure QTextDocument level — no theme, no
# QML runtime.
add_executable(tst_markdownedit
    tst_markdownedit_main.cpp
    tst_markdownedit.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_markdownedit PRIVATE ${CMAKE_SOURCE_DIR}/tests)

target_link_libraries(tst_markdownedit PRIVATE
    awb_markdownedit
    Qt::Core
    Qt::Gui
    Qt::Qml
    Qt::Test
)

add_test(NAME tst_markdownedit COMMAND tst_markdownedit)
