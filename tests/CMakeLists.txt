# Unit tests, one executable per module.
# Every target below is also a ctest test of the same name, so
# `ctest -R tst_core` runs a module and `ctest -R tst_agentcatalog` another.
# BUILD_TESTING and Qt::Test come from the top-level CMakeLists.txt.
add_subdirectory(core)
add_subdirectory(agentcatalog)
add_subdirectory(theme)
add_subdirectory(shell)
add_subdirectory(web)
if (AWB_ENABLE_WEBENGINE)
    add_subdirectory(webengine)
endif()
add_subdirectory(workbench)
add_subdirectory(skillcatalog)
add_subdirectory(tools)
add_subdirectory(markdown)
# 页面层（shell.html + runtime/*.js）的冒烟只在预览能力存在的那条路线上可谈：
# AWB_ENABLE_WEBENGINE=OFF 的连接里既不建这个目标，也没有页面可言（Qt 5 与
# Qt 6 都跑——预览表面在两个大版本上都存在）。
if (AWB_ENABLE_WEBENGINE)
    add_subdirectory(markdownpreview)
    add_subdirectory(markdownpreviewui)
endif()

# Architecture rules as a test: a violating build fails
# instead of relying on people remembering the rules.
add_test(NAME check_architecture
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-architecture.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
