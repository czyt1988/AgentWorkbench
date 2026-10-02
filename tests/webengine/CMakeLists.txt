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

# The compat polyfills injected into every embedded page — same alias as in
# app/CMakeLists.txt, so WebEngineCompat reads the identical :/web/ URL.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/web/webengine/compat-polyfills.js
    PROPERTIES QT_RESOURCE_ALIAS "web/compat-polyfills.js")
awb_add_resources(tst_webengine "test_compat_polyfills"
    PREFIX "/"
    FILES
        ${CMAKE_SOURCE_DIR}/src/web/webengine/compat-polyfills.js
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

# --- Markdown preview runtime (Qt 6 + WebEngine only) ----------------------
# testMarkdownSurfaceServesRuntime loads a real page over awbmd:// and needs
# the same :/markdown/ tree the app embeds: the vendor builds (KaTeX JS/CSS and
# its woff2 fonts) plus this test's own probe files under app/. Mirroring
# app/CMakeLists.txt here is the point — the probe asserts the same path
# mapping (awbmd://runtime/<p> -> :/markdown/<p>) and the same MIME table the
# product uses, so a mapping that only works by accident in one of the two
# targets fails loudly.
if (QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(tst_webengine PRIVATE
        awb_markdown
        awb_markdown_webengine
    )

    awb_add_vendor_resources(tst_webengine "test_markdown_vendor"
        PREFIX "/markdown"
        ALIAS_PREFIX "vendor"
        ROOT ${CMAKE_SOURCE_DIR}/third_party/markdown-runtime
        EXCLUDE "README\\.md$" "/LICENSE$"
    )

    set(_tst_webengine_md_probe ${CMAKE_CURRENT_LIST_DIR}/md-probe.html)
    set(_tst_webengine_md_probe_js ${CMAKE_CURRENT_LIST_DIR}/md-probe-module.js)
    set_source_files_properties(${_tst_webengine_md_probe}
        PROPERTIES QT_RESOURCE_ALIAS "app/md-probe.html")
    set_source_files_properties(${_tst_webengine_md_probe_js}
        PROPERTIES QT_RESOURCE_ALIAS "app/md-probe-module.js")
    awb_add_resources(tst_webengine "test_markdown_probe"
        PREFIX "/markdown"
        FILES
            ${_tst_webengine_md_probe}
            ${_tst_webengine_md_probe_js}
    )
endif()

add_test(NAME tst_webengine COMMAND tst_webengine)
