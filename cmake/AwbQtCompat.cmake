# Qt 5/Qt 6 双版本兼容层。
#
# 顶层 CMakeLists.txt 以 find_package(QT NAMES Qt6 Qt5 ...) 解析出
# QT_VERSION_MAJOR（6.5+ 优先，5.15 兜底），本文件按它分发所有版本相关的
# 构建差异；各模块的 CMakeLists 只调用这里的包装函数、链接 Qt:: 目标。
#
# Qt 5 分支的验证基准是 5.15.16 LTS：内嵌 Web 页依赖的 lifecycleState、
# javaScriptDialogRequested / authenticationDialogRequested、profile 上的
# downloadRequested 等 WebEngine backport 都在 LTS 补丁版里；开源 5.15.0/2
# 未必齐全，Qt 5 路线请使用 5.15.16+。
#
# 版本差异速查（详见各函数注释）：
#   - QtWebEngine Quick 组件名：WebEngineQuick（Qt 6）/ WebEngine（Qt 5）；
#   - qt_add_resources 的 target 签名与 QT_RESOURCE_ALIAS 属性：仅 Qt 6；
#   - qt_add_qml_module：仅 Qt 6，Qt 5 用生成的 qmldir + qrc 镜像同一 URL；
#   - MSVC 的 /utf-8：Qt 6 目标自动注入，Qt 5 需要手动加。

# WebEngine Quick 模块的链接目标。组件在 Qt 6 改名 WebEngine -> WebEngineQuick，
# 消费方一律引用 $AWB_WEBENGINE_TARGET。
if (QT_VERSION_MAJOR EQUAL 6)
    set(AWB_WEBENGINE_TARGET "Qt::WebEngineQuick")
else()
    set(AWB_WEBENGINE_TARGET "Qt::WebEngine")
endif()

# 源码是带中文注释的 UTF-8（coding-standard.md §1）。Qt 6 的 CMake 目标会给
# MSVC 编译注入 /utf-8，Qt 5 不会——不补的话 MSVC 会按本地代码页（如 GBK）
# 解读源文件，中文注释变成 C4819 告警或乱码。
#
# 注意：不要试图用 /FI 或 -include 强制塞 <QDebug> 之类的头——显式 include
# 才是正解；Qt 5 的基础头不传递包含 QDebug，使用 qWarning()/qInfo() 流式
# 输出的文件各自 include 它。
if (QT_VERSION_MAJOR EQUAL 5)
    if (MSVC)
        add_compile_options(/utf-8)
    endif()
endif()

# awb_internal_embed_qrc(<target> <qrc> [DEPENDS <file>...])
# 在「当前目录」登记 rcc 自定义命令并把产物挂到 target 的源列表。
# 不用 qt5_add_resources()：它把命令登记到调用方目录，一旦与目标所在目录
# 不一致（历史上根 CMakeLists 的 i18n），target_sources 的存在性检查就找不
# 到生成文件。Qt 6 侧由 qt_add_resources 自己处理，不走这个函数。
function(awb_internal_embed_qrc target qrc)
    set(_extra_deps "")
    foreach (_d IN LISTS ARGN)
        list(APPEND _extra_deps "${_d}")
    endforeach()
    get_filename_component(_rcc_name "${qrc}" NAME_WE)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/qrc_${_rcc_name}.cpp")
    add_custom_command(OUTPUT "${_out}"
        COMMAND Qt5::rcc --name "${_rcc_name}" --output "${_out}" "${qrc}"
        MAIN_DEPENDENCY "${qrc}"
        DEPENDS ${_extra_deps}
        VERBATIM)
    set_source_files_properties("${_out}" PROPERTIES
        GENERATED TRUE SKIP_AUTOMOC ON SKIP_AUTOUIC ON)
    target_sources(${target} PRIVATE "${_out}")
endfunction()

# awb_add_resources(<target> <name> PREFIX <prefix> FILES <file>...])
# 按每个文件的 QT_RESOURCE_ALIAS 源属性把文件嵌进资源，两个版本产出相同的
# qrc:/ URL。
# Qt 6：直接转发 qt_add_resources()（target 签名，理解 alias 属性）。
# Qt 5：qt5_add_resources() 只有变量签名、也不认 QT_RESOURCE_ALIAS，这里
#       生成一个带 alias 的 .qrc 再交给 rcc（见 awb_internal_embed_qrc）。
#       请在目标所在目录调用（Qt 6 分支无此约束，但保持一致最稳）。
function(awb_add_resources target name)
    set(oneValueArgs PREFIX)
    set(multiValueArgs FILES)
    cmake_parse_arguments(AAR "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (QT_VERSION_MAJOR EQUAL 6)
        qt_add_resources(${target} "${name}"
            PREFIX "${AAR_PREFIX}"
            FILES ${AAR_FILES})
    else()
        set(_qrc "${CMAKE_CURRENT_BINARY_DIR}/awb_${name}.qrc")
        awb_internal_write_qrc("${_qrc}" "${AAR_PREFIX}" ${AAR_FILES})
        awb_internal_embed_qrc(${target} "${_qrc}" ${AAR_FILES})
    endif()
endfunction()

# awb_internal_write_qrc(<out.qrc> <prefix> <file>...)
# 生成 RCC 文件：每个输入按 QT_RESOURCE_ALIAS 源属性取别名（缺省用文件名），
# URL 布局与 Qt 6 的 qt_add_resources 完全一致。路径不含 XML 特殊字符（&<>）。
function(awb_internal_write_qrc out prefix)
    set(_entries "")
    foreach (_f IN LISTS ARGN)
        get_source_file_property(_alias "${_f}" QT_RESOURCE_ALIAS)
        if (NOT _alias)
            get_filename_component(_alias "${_f}" NAME)
        endif()
        string(APPEND _entries "        <file alias=\"${_alias}\">${_f}</file>\n")
    endforeach()
    file(WRITE "${out}"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<RCC>
    <qresource prefix=\"${prefix}\">
${_entries}    </qresource>
</RCC>
")
endfunction()

# awb_internal_qt5_rewritten(<in> <out>)
# 读 <in> 的 QML，给每个无版本的库 import（import QtQuick / QtQuick.Controls
# / QtWebEngine / AgentWorkbench / AgentWorkbench.App）补上兼容版本号，写出
# 到 <out>。Qt 6 允许（并推荐）无版本 import，Qt 5.15 的编译器则硬性要求
# 库 import 带版本（qqmlirbuilder 的 "Library import requires a version"），
# 所以 Qt 5 构建里页面用改写副本，源文件保持 Qt 6 写法不动。
function(awb_internal_qt5_rewritten in out)
    file(READ "${in}" _text)
    # 模块名 → 补上的版本。AgentWorkbench 模块的 qmldir 注册 1.0。
    set(_replacements
        "import QtQuick\n"             "import QtQuick 2.15\n"
        "import QtQuick.Controls\n"    "import QtQuick.Controls 2.15\n"
        "import QtQuick.Layouts\n"     "import QtQuick.Layouts 1.15\n"
        "import QtQuick.Window\n"      "import QtQuick.Window 2.15\n"
        "import QtQuick.Dialogs\n"     "import QtQuick.Dialogs 1.3\n"
        "import QtWebEngine\n"         "import QtWebEngine 1.10\n"
        "import AgentWorkbench\n"      "import AgentWorkbench 1.0\n"
        "import AgentWorkbench.App\n"  "import AgentWorkbench.App 1.0\n")
    while(_replacements)
        list(POP_FRONT _replacements _from _to)
        string(REPLACE "${_from}" "${_to}" _text "${_text}")
    endwhile()
    file(WRITE "${out}" "${_text}")
endfunction()

# awb_add_qml_module(<target> URI <uri> VERSION <x.y> [NO_CACHEGEN]
#                    QML_FILES <file>...)
# 页面 `import AgentWorkbench` 依赖的那个 QML 模块。
# Qt 6：真正的 qt_add_qml_module()（qmldir、模块注册全部自动）。
# Qt 5：没有 QML 模块的 CMake API，这里把同样的文件以相同的 alias 嵌进
#       /qt/qml/<URI>/ 前缀（即 Qt 6 的默认资源前缀，因此 C++ 里所有
#       qrc:/qt/qml/AgentWorkbench/... 的 URL 原样不变），并生成一份 qmldir
#       把每个文件按文件名注册为复合类型——qmldir 支持子目录路径（相对
#       qmldir 解析）。main.cpp 在 Qt 5 分支补 addImportPath("qrc:/qt/qml")
#       后引擎即可解析该模块。QML 源经 awb_internal_qt5_rewritten 补 import
#       版本号（Qt 5 编译器的硬性要求），改写副本放 build 目录。
function(awb_add_qml_module target)
    set(oneValueArgs URI VERSION)
    set(multiValueArgs QML_FILES)
    cmake_parse_arguments(AAM "NO_CACHEGEN" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (QT_VERSION_MAJOR EQUAL 6)
        qt_add_qml_module(${target}
            URI ${AAM_URI}
            VERSION ${AAM_VERSION}
            NO_CACHEGEN
            QML_FILES ${AAM_QML_FILES})
    else()
        # 改写后的 QML 与按源文件名对应的 alias 清单。
        set(_qt5_qml_files "")
        foreach (_f IN LISTS AAM_QML_FILES)
            get_source_file_property(_alias "${_f}" QT_RESOURCE_ALIAS)
            if (NOT _alias)
                get_filename_component(_alias "${_f}" NAME)
            endif()
            set(_rewritten "${CMAKE_CURRENT_BINARY_DIR}/qt5qml/${_alias}")
            get_filename_component(_dir "${_rewritten}" DIRECTORY)
            file(MAKE_DIRECTORY "${_dir}")
            awb_internal_qt5_rewritten("${_f}" "${_rewritten}")
            set_source_files_properties("${_rewritten}" PROPERTIES
                QT_RESOURCE_ALIAS "${_alias}")
            list(APPEND _qt5_qml_files "${_rewritten}")
        endforeach()

        # qmldir：每个 QML 文件注册为「文件名同名 + 版本 + alias 路径」的
        # 复合类型，与 qt_add_qml_module 的类型名规则一致。
        set(_entries "")
        foreach (_f IN LISTS AAM_QML_FILES)
            get_filename_component(_type "${_f}" NAME_WE)
            get_source_file_property(_alias "${_f}" QT_RESOURCE_ALIAS)
            if (NOT _alias)
                get_filename_component(_alias "${_f}" NAME)
            endif()
            string(APPEND _entries "${_type} ${AAM_VERSION} ${_alias}\n")
        endforeach()
        set(_qmldir "${CMAKE_CURRENT_BINARY_DIR}/awb_${AAM_URI}.qmldir")
        file(WRITE "${_qmldir}" "module ${AAM_URI}\n${_entries}")
        # qmldir 的资源别名必须是裸 "qmldir"（引擎按 <导入路径>/<URI>/qmldir 找）。
        set_source_files_properties("${_qmldir}" PROPERTIES
            QT_RESOURCE_ALIAS "qmldir")
        set(_qrc "${CMAKE_CURRENT_BINARY_DIR}/awb_${AAM_URI}-qml.qrc")
        awb_internal_write_qrc("${_qrc}" "/qt/qml/${AAM_URI}"
            ${_qt5_qml_files} "${_qmldir}")
        awb_internal_embed_qrc(${target} "${_qrc}" ${_qt5_qml_files} "${_qmldir}")
    endif()
endfunction()
