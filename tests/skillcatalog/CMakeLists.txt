# tst_skillcatalog — frontmatter parsing + scanning.
qt_add_executable(tst_skillcatalog
    tst_skills_main.cpp
    tst_skillfrontmatter.cpp
    tst_skillscanner.cpp
    tst_skillsfacade.cpp
    ${CMAKE_SOURCE_DIR}/tests/awbtest_runner.cpp
)

target_include_directories(tst_skillcatalog PRIVATE ${CMAKE_SOURCE_DIR}/tests)

target_link_libraries(tst_skillcatalog PRIVATE
    awb_skillcatalog
    awb_core
    awb_theme
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME tst_skillcatalog COMMAND tst_skillcatalog)
