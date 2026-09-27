# tst_tools — workspace store, lazy file tree model, icon map, facade wiring.
qt_add_executable(tst_tools
    tst_tools_main.cpp
    tst_toolsstore.cpp
    tst_filetreemodel.cpp
    tst_fileicons.cpp
    tst_toolsfacade.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_tools PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# The bundled icon map is an absolute path outside this directory; the alias
# must be declared before qt_add_resources() processes the file. FileIcons
# reads it from the same :/config/ URL the executable uses.
set(_tst_tools_default_icons ${CMAKE_SOURCE_DIR}/config/default_file_icons.json)
set_source_files_properties(${_tst_tools_default_icons}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_file_icons.json")

qt_add_resources(tst_tools "test_resources"
    PREFIX "/"
    FILES
        ${_tst_tools_default_icons}
)

target_link_libraries(tst_tools PRIVATE
    awb_tools
    awb_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME tst_tools COMMAND tst_tools)
