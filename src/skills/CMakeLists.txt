# awb_skills (L2 domain): discovery and presentation of local skills.
# Depends on awb_core/awb_theme only — never on shell or other domains
# (specs/01-architecture.md §5.1).
qt_add_library(awb_skills STATIC
    SkillDefinition.h
    SkillFrontmatter.cpp
    SkillFrontmatter.h
    SkillModel.cpp
    SkillModel.h
    SkillRoot.h
    SkillRoots.cpp
    SkillScanner.cpp
    SkillScanner.h
    SkillsFacade.cpp
    SkillsFacade.h
)

target_include_directories(awb_skills PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_skills
    PUBLIC
        awb_core
        awb_theme
        Qt6::Core
        Qt6::Gui
)
