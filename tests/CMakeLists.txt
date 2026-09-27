# Unit tests, one executable per module.
# Every target below is also a ctest test of the same name, so
# `ctest -R tst_core` runs a module and `ctest -R tst_agentcatalog` another.
# BUILD_TESTING and Qt6::Test come from the top-level CMakeLists.txt.
add_subdirectory(core)
add_subdirectory(agentcatalog)
add_subdirectory(theme)
add_subdirectory(shell)
add_subdirectory(web)
add_subdirectory(skillcatalog)

# Architecture rules as a test: a violating build fails
# instead of relying on people remembering the rules.
add_test(NAME check_architecture
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-architecture.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
