# tst_skills — frontmatter parsing + scanning.
qt_add_executable(tst_skills
    tst_skills_main.cpp
    tst_skillfrontmatter.cpp
    tst_skillscanner.cpp
    tst_skillsfacade.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_skills PRIVATE ${CMAKE_SOURCE_DIR}/tests)

target_link_libraries(tst_skills PRIVATE
    awb_skills
    awb_core
    awb_theme
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME tst_skills COMMAND tst_skills)
