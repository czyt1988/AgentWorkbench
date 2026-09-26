# tst_agents — the legacy config/model/launcher suite (S1 interim: one file;
# specs/03 §5 maps its cases onto per-class files during S2).

# The bundled defaults are an absolute path outside this directory; the
# alias must be declared before qt_add_resources() processes the file.
set(_tst_agents_default_config ${CMAKE_SOURCE_DIR}/config/default_agents.json)
set_source_files_properties(${_tst_agents_default_config}
    PROPERTIES QT_RESOURCE_ALIAS "config/default_agents.json")

qt_add_executable(tst_agents
    tst_agents.cpp
    ${CMAKE_SOURCE_DIR}/src/AgentConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/AgentConfig.h
    ${CMAKE_SOURCE_DIR}/src/AgentModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AgentModel.h
    ${CMAKE_SOURCE_DIR}/src/AgentLauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/AgentLauncher.h
)

target_include_directories(tst_agents PRIVATE ${CMAKE_SOURCE_DIR}/src)

qt_add_resources(tst_agents "test_resources"
    PREFIX "/"
    FILES
        ${_tst_agents_default_config}
)

target_link_libraries(tst_agents PRIVATE
    awb_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_agents COMMAND tst_agents)
