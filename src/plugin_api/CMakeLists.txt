# awb_plugin_api (L0): the plugin ABI — a header-only interface target
# that external repositories link against (specs/01-architecture.md §9).
# Depends on Qt Core only; never on any host module.
add_library(awb_plugin_api INTERFACE)

target_include_directories(awb_plugin_api INTERFACE ${CMAKE_SOURCE_DIR}/src)
