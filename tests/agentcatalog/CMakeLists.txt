# tst_agentcatalog — the agent catalog suite: repository sync
# semantics, model roles, facade CRUD, script logging, URL/token handling,
# runtime launch/stop/force-stop.
qt_add_executable(tst_agentcatalog
    tst_agents_main.cpp
    tst_agentmodel.cpp
    tst_agentrepository.cpp
    tst_agentscripts.cpp
    tst_agentsfacade.cpp
    tst_agentruntime.cpp
    tst_agenturls.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_agentcatalog PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# The bundled defaults are an absolute path outside this directory; the
# alias must be declared before qt_add_resources() processes the file.
set(_tst_agentcatalog_default_config ${CMAKE_SOURCE_DIR}/config/default_agents.json)
set_source_files_properties(${_tst_agentcatalog_default_config}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_agents.json")

qt_add_resources(tst_agentcatalog "test_resources"
    PREFIX "/"
    FILES
        ${_tst_agentcatalog_default_config}
)

target_link_libraries(tst_agentcatalog PRIVATE
    awb_agentcatalog
    awb_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_agentcatalog COMMAND tst_agentcatalog)
