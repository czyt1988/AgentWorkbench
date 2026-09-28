# awb_skillcatalog (L2 domain): discovery and presentation of local skills.
# Depends on awb_core/awb_theme only — never on shell or other domains
add_library(awb_skillcatalog STATIC
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

target_include_directories(awb_skillcatalog PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_skillcatalog
    PUBLIC
        awb_core
        awb_theme
        Qt::Core
        Qt::Gui
)
