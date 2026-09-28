# tst_shell — window-skeleton suite.
# Single test class -> plain QTEST_MAIN (needs a QApplication for the
# clipboard, which the shared registry runner's QCoreApplication cannot
# provide).
add_executable(tst_shell
    tst_shell.cpp
)

target_link_libraries(tst_shell PRIVATE
    awb_shell
    awb_core
    awb_theme
    Qt::Core
    Qt::Gui
    Qt::Test
)

add_test(NAME tst_shell COMMAND tst_shell)
