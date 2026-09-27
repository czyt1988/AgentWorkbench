# Build options for AgentWorkbench.
#
# AWB_ENABLE_WEBENGINE: embedded Web views via Qt WebEngine. WebEngine only
# ships for MSVC — combining ON with MinGW must fail at configure time with
# a readable message, not with a wall of linker errors.

option(AWB_ENABLE_WEBENGINE "Embed agent WebUIs with Qt WebEngine" ON)

if (AWB_ENABLE_WEBENGINE AND WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR
        "AWB_ENABLE_WEBENGINE=ON requires MSVC: Qt WebEngine has no MinGW "
        "build. Configure with -DAWB_ENABLE_WEBENGINE=OFF (external browser "
        "surface) or build with the MSVC toolchain.")
endif()

if (AWB_ENABLE_WEBENGINE)
    find_package(Qt6 6.5 REQUIRED COMPONENTS WebEngineQuick)
endif()
