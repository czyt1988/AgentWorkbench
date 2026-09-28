# awb_theme (L1): theme JSON -> semantic tokens -> QML properties.
# Links Qt Core + Gui (QColor) only — no Qt Quick.
add_library(awb_theme STATIC
    Theme.cpp
    Theme.h
    ThemeFile.h
    ThemeLoader.cpp
    ThemeLoader.h
    ThemeRegistry.cpp
    ThemeRegistry.h
)

target_include_directories(awb_theme PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_theme
    PUBLIC
        awb_core
        Qt::Core
        Qt::Gui
)
