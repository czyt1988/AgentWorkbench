# tst_tools — workspace store, lazy file tree model, facade wiring.
qt_add_executable(tst_tools
    tst_tools_main.cpp
    tst_toolsstore.cpp
    tst_filetreemodel.cpp
    tst_toolsfacade.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_tools PRIVATE ${CMAKE_SOURCE_DIR}/tests)

target_link_libraries(tst_tools PRIVATE
    awb_tools
    awb_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME tst_tools COMMAND tst_tools)
