# tst_core — infrastructure suite for awb_core.
qt_add_executable(tst_core
    tst_core_main.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
    tst_envexpander.cpp
    tst_iconresolver.cpp
    tst_httpprobe.cpp
    tst_jsonstore.cpp
    tst_legacyimport.cpp
    tst_logging.cpp
    tst_paths.cpp
    tst_pluginhost.cpp
    tst_processrunner.cpp
    tst_scrptrunner.cpp
    tst_settings.cpp
)

target_include_directories(tst_core PRIVATE ${CMAKE_SOURCE_DIR}/tests)

target_link_libraries(tst_core PRIVATE
    awb_core
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_core COMMAND tst_core)
