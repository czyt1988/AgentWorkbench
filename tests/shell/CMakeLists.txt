# tst_shell — window-skeleton suite.
# Single test class -> plain QTEST_MAIN (needs a QApplication for the
# clipboard, which the shared registry runner's QCoreApplication cannot
# provide).
add_executable(tst_shell
    tst_shell.cpp
)

# 只为 awbtest.h（QML 方法类型守卫）：tst_shell 不经共享注册表跑。
target_include_directories(tst_shell PRIVATE ${CMAKE_SOURCE_DIR}/tests)

target_link_libraries(tst_shell PRIVATE
    awb_shell
    awb_core
    awb_theme
    Qt::Core
    Qt::Gui
    Qt::Test
)

add_test(NAME tst_shell COMMAND tst_shell)
