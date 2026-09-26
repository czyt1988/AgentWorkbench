# Unit tests, one executable per module (specs/01-architecture.md §11).
# Every target below is also a ctest test of the same name, so
# `ctest -R tst_core` runs a module and `ctest -R tst_agents` another.
# BUILD_TESTING and Qt6::Test come from the top-level CMakeLists.txt.
add_subdirectory(core)
add_subdirectory(agents)
add_subdirectory(theme)
add_subdirectory(shell)
add_subdirectory(web)
add_subdirectory(skills)

# Architecture rules as a test (specs/01 §5.3): a violating build fails
# instead of relying on people remembering the rules.
add_test(NAME check_architecture
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-architecture.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
