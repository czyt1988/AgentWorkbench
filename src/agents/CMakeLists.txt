# awb_agents (L2 domain): agent definitions, persistence, process
# lifecycle, health checks and one-shot commands. Depends on awb_core only
# (no Qt Quick, no other domain modules — specs/01-architecture.md §5.1).
qt_add_library(awb_agents STATIC
    AgentDefinition.h
    AgentHealthMonitor.cpp
    AgentHealthMonitor.h
    AgentModel.cpp
    AgentModel.h
    AgentRepository.cpp
    AgentRepository.h
    AgentRuntime.cpp
    AgentRuntime.h
    AgentScripts.cpp
    AgentScripts.h
    AgentState.h
    AgentStateStore.cpp
    AgentStateStore.h
    AgentUrls.cpp
    AgentUrls.h
    AgentsFacade.cpp
    AgentsFacade.h
)

target_include_directories(awb_agents PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_agents
    PUBLIC
        awb_core
        Qt6::Core
        Qt6::Gui
)
