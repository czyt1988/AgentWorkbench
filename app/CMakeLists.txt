# L4 executable: assembly + the QML module
# + every embedded resource. No business logic lives here.
#
# All module QML is compiled into the executable through awb_add_qml_module
# (qt_add_qml_module on Qt 6, a generated qmldir+qrc on Qt 5) with stable
# aliases, so a page URL looks like
#   qrc:/qt/qml/AgentWorkbench/agentcatalog/AgentGridPage.qml
# Resources are never put into static libraries (qrc initializers in static
# libraries get dropped by the linker.

set(_shell_qml MainWindow Sidebar Workspace StatusBar Toasts PageHeader
               SettingsPage
               SettingsAppearancePage SettingsLaunchersPage
               SettingsEnvironmentPage SettingsSkillsPage SettingsWebPage
               SettingsPluginsPage SettingsAdvancedPage
)
set(_component_qml AButton AIconButton ASearchField ACard APill ADialog
                   AConfirmDialog AAlertDialog AEmptyState ASectionHeader
                   AToastStack AStatusDot ATextField AFormLabel AListRow
                   ATextArea AScrollBar AgentAvatar ASpotlight
                   AWorkspaceGlow AMenu AMenuItem AMenuSeparator
                   AComboBox AColorPicker AColorSwatch AColorField
                   ASplitHandle)
set(_agentcatalog_qml AgentCard AgentGridPage AgentEditDialog)
set(_skillcatalog_qml SkillGridPage SkillCard SkillDetailFlyout)
set(_tools_qml MarkdownContextMenu PromptChecksPopup DraftHistoryPopup)
# The Markdown workbench page lives in the workbench domain: it spans the
# tools (draft, workspace tree) and markdown (preview session) domains, so
# only L3 may reference both. Registered in EVERY build — the prompt mode is
# a plain QML editor and works without WebEngine (the preview pane is loaded
# by URL only where it exists, see _markdown_pane_qml below).
set(_workbench_qml MarkdownWorkbenchPage WorkbenchToolbar)
# The SARibbon command bar exists only in AWB_ENABLE_RIBBON builds. When the
# URL is absent the page's command-bar Loader fails and falls back to
# WorkbenchToolbar.qml, which is always registered.
set(_workbench_ribbon_qml WorkbenchRibbon)
set(_web_qml WebTabsPage)
set(_webengine_qml WebEngineSurface)
set(_markdown_pane_qml MarkdownPreviewPane)

set(AWB_QML_FILES "")
foreach(name IN LISTS _skillcatalog_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/skillcatalog/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "skillcatalog/${name}.qml")
endforeach()
foreach(name IN LISTS _web_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/web/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "web/${name}.qml")
endforeach()
foreach(name IN LISTS _webengine_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/web/webengine/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "web/${name}.qml")
endforeach()
foreach(name IN LISTS _shell_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/shell/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "shell/${name}.qml")
endforeach()
foreach(name IN LISTS _component_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/shell/qml/components/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "components/${name}.qml")
endforeach()
foreach(name IN LISTS _agentcatalog_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/agentcatalog/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "agentcatalog/${name}.qml")
endforeach()
foreach(name IN LISTS _tools_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/tools/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "tools/${name}.qml")
endforeach()
foreach(name IN LISTS _workbench_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/workbench/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "workbench/${name}.qml")
endforeach()
if (AWB_ENABLE_RIBBON)
    foreach(name IN LISTS _workbench_ribbon_qml)
        set(_f ${CMAKE_SOURCE_DIR}/src/workbench/qml/${name}.qml)
        list(APPEND AWB_QML_FILES ${_f})
        set_source_files_properties(${_f} PROPERTIES
            QT_RESOURCE_ALIAS "workbench/${name}.qml")
    endforeach()
endif()
# Markdown-domain QML that needs no WebEngine: the outline panel is plain
# QtQuick, so it is registered in EVERY build and the workbench page
# instantiates it like any other component. Only the preview pane below is
# capability-gated.
set(_markdown_qml MarkdownOutlinePanel)
foreach(name IN LISTS _markdown_qml)
    set(_f ${CMAKE_SOURCE_DIR}/src/markdown/qml/${name}.qml)
    list(APPEND AWB_QML_FILES ${_f})
    set_source_files_properties(${_f} PROPERTIES
        QT_RESOURCE_ALIAS "markdown/${name}.qml")
endforeach()

# The preview pane is registered only where the feature can actually run:
# WebEngine builds (both Qt majors), the same condition as the /markdown
# runtime embed below and MarkdownRuntime::isSupportedBuild(). Elsewhere the
# file is not in the binary at all — not a broken half of it. The workbench
# page reaches it by resource URL (see its Loader), so an unregistered pane
# degrades to prompt mode only.
if (AWB_ENABLE_WEBENGINE)
    foreach(name IN LISTS _markdown_pane_qml)
        set(_f ${CMAKE_SOURCE_DIR}/src/markdown/qml/${name}.qml)
        list(APPEND AWB_QML_FILES ${_f})
        set_source_files_properties(${_f} PROPERTIES
            QT_RESOURCE_ALIAS "markdown/${name}.qml")
    endforeach()
endif()

add_executable(AgentWorkbench
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/app.rc
)

target_include_directories(AgentWorkbench
    PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(AgentWorkbench PRIVATE
    SingleApplication::SingleApplication
    awb_agentcatalog
    awb_core
    awb_markdown
    awb_shell
    awb_skillcatalog
    awb_theme
    awb_tools
    awb_web
    awb_workbench
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Qml
    Qt::Quick
    Qt::QuickControls2
    Qt::Network
)

if (AWB_ENABLE_WEBENGINE)
    target_compile_definitions(AgentWorkbench PRIVATE AWB_ENABLE_WEBENGINE=1)
    target_link_libraries(AgentWorkbench PRIVATE awb_web_webengine
                                                 ${AWB_WEBENGINE_TARGET})
    # The markdown preview surface (awbmd:// handler + preview profile). Same
    # condition as the :/markdown/ embed and AWB_MD_PREVIEW_SUPPORTED below:
    # linking the adapter is what makes the QML window able to resolve
    # awbmd://runtime/… at all.
    target_link_libraries(AgentWorkbench PRIVATE awb_markdown_webengine)
endif()

# The ribbon of the Markdown workbench page (AWB_ENABLE_RIBBON, see the root
# CMakeLists). The app target is the single link point: domain libraries never
# link SARibbon, QML just does `import SARibbon` after main.cpp registered the
# types imperatively before engine.load().
if (AWB_ENABLE_RIBBON)
    target_compile_definitions(AgentWorkbench PRIVATE AWB_ENABLE_RIBBON=1)
    target_link_libraries(AgentWorkbench PRIVATE SARibbon::Qml)
endif()

# The QML module shared by every page/component. awb_add_qml_module (from
# cmake/AwbQtCompat.cmake) is qt_add_qml_module on Qt 6; on Qt 5 it embeds the
# same files under the same aliases plus a generated qmldir so the
# qrc:/qt/qml/AgentWorkbench/... URLs resolve identically.
awb_add_qml_module(AgentWorkbench
    URI AgentWorkbench
    VERSION 1.0
    QML_FILES ${AWB_QML_FILES}
)

# --- Embedded resources (executable only, never a library) -------------------
# Compiled translations, embedded under :/i18n/. The alias + embed live here
# (same directory scope as the target) on both Qt versions; the .qm paths in
# QM_FILES come from the qt_create_translation() call in the root CMakeLists.
# The target dependency guarantees lrelease finishes before rcc reads the .qm
# (they are registered in different directories).
add_dependencies(AgentWorkbench awb_translations)
set_source_files_properties(${QM_FILES} PROPERTIES
    QT_RESOURCE_ALIAS "agentworkbench_zh_CN.qm")
awb_add_resources(AgentWorkbench "i18n"
    PREFIX "/i18n"
    FILES
        ${QM_FILES}
)

set(AWB_ICON_FILES
    ${CMAKE_SOURCE_DIR}/icons/app-icon.png
    ${CMAKE_SOURCE_DIR}/icons/bot.svg
    ${CMAKE_SOURCE_DIR}/icons/bold.svg
    ${CMAKE_SOURCE_DIR}/icons/bullet-list.svg
    ${CMAKE_SOURCE_DIR}/icons/chevron-down.svg
    ${CMAKE_SOURCE_DIR}/icons/chevron-left.svg
    ${CMAKE_SOURCE_DIR}/icons/chevron-right.svg
    ${CMAKE_SOURCE_DIR}/icons/close.svg
    ${CMAKE_SOURCE_DIR}/icons/code.svg
    ${CMAKE_SOURCE_DIR}/icons/copy.svg
    ${CMAKE_SOURCE_DIR}/icons/cube.svg
    ${CMAKE_SOURCE_DIR}/icons/default.svg
    ${CMAKE_SOURCE_DIR}/icons/deepseek-harness.svg
    ${CMAKE_SOURCE_DIR}/icons/download-hover.svg
    ${CMAKE_SOURCE_DIR}/icons/download.svg
    ${CMAKE_SOURCE_DIR}/icons/external-link.svg
    ${CMAKE_SOURCE_DIR}/icons/folder.svg
    ${CMAKE_SOURCE_DIR}/icons/gear.svg
    ${CMAKE_SOURCE_DIR}/icons/home.svg
    ${CMAKE_SOURCE_DIR}/icons/kimi-code.svg
    ${CMAKE_SOURCE_DIR}/icons/menu.svg
    ${CMAKE_SOURCE_DIR}/icons/openclaw.svg
    ${CMAKE_SOURCE_DIR}/icons/opencode.svg
    ${CMAKE_SOURCE_DIR}/icons/paste.svg
    ${CMAKE_SOURCE_DIR}/icons/plus.svg
    ${CMAKE_SOURCE_DIR}/icons/qwen-code.svg
    ${CMAKE_SOURCE_DIR}/icons/refresh.svg
    ${CMAKE_SOURCE_DIR}/icons/search.svg
    ${CMAKE_SOURCE_DIR}/icons/skills.svg
    ${CMAKE_SOURCE_DIR}/icons/terminal.svg
    ${CMAKE_SOURCE_DIR}/icons/file.svg
    ${CMAKE_SOURCE_DIR}/icons/tools.svg
    ${CMAKE_SOURCE_DIR}/icons/undo.svg
    ${CMAKE_SOURCE_DIR}/icons/redo.svg
    ${CMAKE_SOURCE_DIR}/icons/cut.svg
    ${CMAKE_SOURCE_DIR}/icons/web.svg
)
foreach(icon IN LISTS AWB_ICON_FILES)
    get_filename_component(_name ${icon} NAME)
    set_source_files_properties(${icon} PROPERTIES
        QT_RESOURCE_ALIAS "icons/${_name}")
endforeach()

# File-type and folder-type icons for the Agent Tools file tree. Which name or
# suffix maps to which icon is data, not code: config/default_file_icons.json.
set(AWB_FILETYPE_ICON_FILES
    ${CMAKE_SOURCE_DIR}/icons/filetypes/archive.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/cfamily.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/cmake.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/code.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/config.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/image.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/json.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/markdown.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/markup.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/pdf.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/python.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/qml.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/shell.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/style.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/table.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/text.svg
    ${CMAKE_SOURCE_DIR}/icons/filetypes/typescript.svg
)
set(AWB_FOLDERTYPE_ICON_FILES
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/assets.svg
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/build.svg
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/config.svg
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/deps.svg
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/docs.svg
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/scripts.svg
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/src.svg
    ${CMAKE_SOURCE_DIR}/icons/foldertypes/test.svg
)
foreach(icon IN LISTS AWB_FILETYPE_ICON_FILES)
    get_filename_component(_name ${icon} NAME)
    set_source_files_properties(${icon} PROPERTIES
        QT_RESOURCE_ALIAS "icons/filetypes/${_name}")
endforeach()
foreach(icon IN LISTS AWB_FOLDERTYPE_ICON_FILES)
    get_filename_component(_name ${icon} NAME)
    set_source_files_properties(${icon} PROPERTIES
        QT_RESOURCE_ALIAS "icons/foldertypes/${_name}")
endforeach()

set_source_files_properties(${CMAKE_SOURCE_DIR}/config/default_agents.json
    PROPERTIES QT_RESOURCE_ALIAS "config/default_agents.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/config/default_file_icons.json
    PROPERTIES QT_RESOURCE_ALIAS "config/default_file_icons.json")
# Built-in prompt templates (M2): data, not code — the library reads them
# from :/config/ the same way FileIcons reads its tables.
set_source_files_properties(${CMAKE_SOURCE_DIR}/config/default_prompt_templates.json
    PROPERTIES QT_RESOURCE_ALIAS "config/default_prompt_templates.json")
# Old-engine JS polyfills injected into every embedded page (webengine
# adapter reads :/web/compat-polyfills.js).
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/web/webengine/compat-polyfills.js
    PROPERTIES QT_RESOURCE_ALIAS "web/compat-polyfills.js")
# Command-bar icon glyphs as a JS library (data-URL SVG tinting). Placed in
# the QML module's URL space so the ribbon's relative import
# `import "RibbonIcons.js"` resolves on both Qt generations without going
# through the Qt 5 qmldir rewrite.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/workbench/qml/RibbonIcons.js
    PROPERTIES QT_RESOURCE_ALIAS
    "qt/qml/AgentWorkbench/workbench/RibbonIcons.js")

awb_add_resources(AgentWorkbench "app_resources"
    PREFIX "/"
    FILES
        ${CMAKE_SOURCE_DIR}/config/default_agents.json
        ${CMAKE_SOURCE_DIR}/config/default_file_icons.json
        ${CMAKE_SOURCE_DIR}/config/default_prompt_templates.json
        ${CMAKE_SOURCE_DIR}/src/web/webengine/compat-polyfills.js
        ${CMAKE_SOURCE_DIR}/src/workbench/qml/RibbonIcons.js
        ${AWB_ICON_FILES}
        ${AWB_FILETYPE_ICON_FILES}
        ${AWB_FOLDERTYPE_ICON_FILES}
)

# Built-in themes.
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
    PROPERTIES QT_RESOURCE_ALIAS "mocha-dark.json")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
    PROPERTIES QT_RESOURCE_ALIAS "latte-light.json")

awb_add_resources(AgentWorkbench "theme_resources"
    PREFIX "/themes"
    FILES
        ${CMAKE_SOURCE_DIR}/resources/themes/mocha-dark.json
        ${CMAKE_SOURCE_DIR}/resources/themes/latte-light.json
)

# --- Markdown preview runtime (WebEngine builds) ----------------------------
# Third-party browser builds served to the embedded view over awbmd://. They
# are embedded as-is (no build-time bundling) because the app must render
# offline and a packaging machine may have no network at all.
#
# The guard tracks the capability gate one-to-one (see
# src/markdown/MarkdownRuntime.cpp's isSupportedBuild() and the pane
# registration above): a binary either has the surface, the pane and these
# ~4 MiB together, or none of them. Qt 5 notes: markdown-it / KaTeX /
# highlight.js run on Chromium 87; mermaid does not (its build uses post-87
# syntax) and the page degrades diagrams to the per-block error placeholder.
if (AWB_ENABLE_WEBENGINE)
    awb_add_vendor_resources(AgentWorkbench "markdown_vendor"
        PREFIX "/markdown"
        ALIAS_PREFIX "vendor"
        ROOT ${CMAKE_SOURCE_DIR}/third_party/markdown-runtime
        # Prose and licences stay in the repo, not in the binary.
        EXCLUDE "README\\.md$" "/LICENSE$"
    )
    # Our own page runtime, under the same /markdown prefix the scheme handler
    # resolves against, so the page fetches awbmd://runtime/app/renderer.js.
    # Alias "app" and ROOT must match tests/markdown/CMakeLists.txt: that test
    # reads this exact tree to check the JS command names against the C++
    # protocol table, and a mismatch here means the binary ships a runtime the
    # tests never looked at.
    awb_add_vendor_resources(AgentWorkbench "markdown_page_runtime"
        PREFIX "/markdown"
        ALIAS_PREFIX "app"
        ROOT ${CMAKE_SOURCE_DIR}/src/markdown/webengine/runtime
    )
endif()

# Win32 GUI subsystem (no console window) on Windows.
set_target_properties(AgentWorkbench PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
