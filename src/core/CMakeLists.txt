# awb_core (L0): infrastructure with no business knowledge and no UI
# dependency. Public include root is src/, so consumers write
# `#include "core/Paths.h"`.
qt_add_library(awb_core STATIC
    EnvExpander.cpp
    EnvExpander.h
    HttpProbe.cpp
    HttpProbe.h
    IconResolver.cpp
    IconResolver.h
    JsonStore.cpp
    JsonStore.h
    LegacyImport.cpp
    LegacyImport.h
    Logging.cpp
    Logging.h
    OpResult.cpp
    OpResult.h
    Paths.cpp
    Paths.h
    PluginHost.cpp
    PluginHost.h
    ProcessRunner.cpp
    ProcessRunner.h
    ScriptRunner.cpp
    ScriptRunner.h
    Settings.cpp
    Settings.h
    TextUtils.cpp
    TextUtils.h
)

target_include_directories(awb_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_core PUBLIC awb_plugin_api)

# spdlog backs core::Logging only; PRIVATE so no other layer sees its headers.
target_link_libraries(awb_core PRIVATE spdlog::spdlog)

target_link_libraries(awb_core
    PUBLIC
        Qt6::Core
        Qt6::Network
)
