# tst_webengine — embedded surface load smoke.
# The surface QML and the single component it references (AButton) are
# embedded under the same module URLs/aliases as in app/CMakeLists.txt, so
# the engine resolves exactly what the per-tab Loader loads. A new
# component dependency fails loudly here ("unknown type") — add it below.
add_executable(tst_webengine
    tst_webengine.cpp
)

target_include_directories(tst_webengine PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(tst_webengine PRIVATE
    awb_web_webengine
    awb_web
    awb_theme
    awb_core
    Qt::Core
    Qt::Gui
    Qt::Qml
    Qt::Test
)

# The theme.* tokens used by the surface come from the built-in themes
# (embedded the same way tst_theme does).
set(_tst_webengine_themes
    ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json)
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
    PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")
awb_add_resources(tst_webengine "test_themes"
    PREFIX "/themes"
    FILES
        ${_tst_webengine_themes}
)

set(_tst_webengine_qml
    ${CMAKE_SOURCE_DIR}/src/web/webengine/qml/WebEngineSurface.qml
    ${CMAKE_SOURCE_DIR}/src/shell/qml/components/AButton.qml)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/web/webengine/qml/WebEngineSurface.qml
    PROPERTIES QT_RESOURCE_ALIAS "web/WebEngineSurface.qml")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/shell/qml/components/AButton.qml
    PROPERTIES QT_RESOURCE_ALIAS "components/AButton.qml")
awb_add_qml_module(tst_webengine
    URI AgentWorkbench
    VERSION 1.0
    QML_FILES ${_tst_webengine_qml}
)

add_test(NAME tst_webengine COMMAND tst_webengine)
