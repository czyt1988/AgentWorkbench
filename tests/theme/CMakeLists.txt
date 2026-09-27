# tst_theme — theme engine suite: loader validation rules
# and registry built-in/user override behaviour.
qt_add_executable(tst_theme
    tst_theme_main.cpp
    tst_themeloader.cpp
    tst_themeregistry.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_theme PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# Built-in themes for the loader/registry tests (:/themes/...).
set(_tst_theme_files
    ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json)
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
    PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")

qt_add_resources(tst_theme "test_themes"
    PREFIX "/themes"
    FILES
        ${_tst_theme_files}
)

target_link_libraries(tst_theme PRIVATE
    awb_theme
    awb_core
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME tst_theme COMMAND tst_theme)
