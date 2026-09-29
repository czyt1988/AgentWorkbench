# awb_shell (L2, UI framework): window skeleton and generic UI facilities.
# Knows nothing about agents/skills/web.
add_library(awb_shell STATIC
    NavigationModel.cpp
    NavigationModel.h
    Notifications.cpp
    Notifications.h
    PageDescriptor.h
    ShellController.cpp
    ShellController.h
    UiServices.cpp
    UiServices.h
)

target_include_directories(awb_shell PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(awb_shell
    PUBLIC
        awb_core
        awb_theme
        Qt::Core
        Qt::Gui
)

# ChooseColorW（UiServices::pickColor 的原生颜色对话框）在 comdlg32。
if (WIN32)
    target_link_libraries(awb_shell PUBLIC comdlg32)
endif()
