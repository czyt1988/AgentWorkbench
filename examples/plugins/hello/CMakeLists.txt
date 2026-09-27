# The example plugin: a SHARED library exporting the two
# C entry points, with its own QML shipped as a plugin-side resource
# (shared-library qrc initializers run on load — unlike static libraries).
qt_add_library(awb_example_hello SHARED
    hello.cpp
)

target_link_libraries(awb_example_hello PRIVATE
    awb_plugin_api
    Qt6::Core
)

set_target_properties(awb_example_hello PROPERTIES
    OUTPUT_NAME "hello"
    PREFIX ""
)

qt_add_resources(awb_example_hello "hello_resources"
    PREFIX "/hello"
    FILES
        HelloPage.qml
)

# Install next to plugin.json in the developer's data directory so the
# host's discovery (which only scans <dataRoot>/plugins) finds it.
set(_hello_dest "$ENV{USERPROFILE}/.AgentWorkbench/plugins/hello")
add_custom_command(TARGET awb_example_hello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_hello_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:awb_example_hello>
            "${_hello_dest}/"
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/plugin.json "${_hello_dest}/"
    COMMENT "Installing the hello example plugin to ${_hello_dest}"
)
