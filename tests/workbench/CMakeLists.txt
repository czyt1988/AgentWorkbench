# tst_workbench — the cross-domain hub (WorkbenchContext):
# openWeb's tab creation + navigation, openWebExternal's browser path.
qt_add_executable(tst_workbench
    tst_workbench.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_workbench PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# The bundled defaults are an absolute path outside this directory; the
# alias must be declared before qt_add_resources() processes the file
# (the repository seeds the data root from the qrc copy).
set(_tst_workbench_default_config ${CMAKE_SOURCE_DIR}/config/default_agents.json)
set_source_files_properties(${_tst_workbench_default_config}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_agents.json")

qt_add_resources(tst_workbench "test_resources"
    PREFIX "/"
    FILES
        ${_tst_workbench_default_config}
)

target_link_libraries(tst_workbench PRIVATE
    awb_workbench
    awb_agentcatalog
    awb_web
    awb_shell
    awb_core
    awb_theme
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_workbench COMMAND tst_workbench)
