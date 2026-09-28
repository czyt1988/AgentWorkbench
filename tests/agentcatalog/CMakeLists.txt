# tst_agentcatalog — the agent catalog suite: repository sync
# semantics, model roles, facade CRUD, script logging, URL/token handling,
# runtime launch/stop/force-stop.
add_executable(tst_agentcatalog
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
# alias must be declared before awb_add_resources() processes the file.
set(_tst_agentcatalog_default_config ${CMAKE_SOURCE_DIR}/config/default_agents.json)
set_source_files_properties(${_tst_agentcatalog_default_config}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_agents.json")

awb_add_resources(tst_agentcatalog "test_resources"
    PREFIX "/"
    FILES
        ${_tst_agentcatalog_default_config}
)

target_link_libraries(tst_agentcatalog PRIVATE
    awb_agentcatalog
    awb_core
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Test
)

add_test(NAME tst_agentcatalog COMMAND tst_agentcatalog)
