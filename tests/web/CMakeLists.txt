# tst_web — web tabs suite (specs/01 §11). Deliberately does NOT link
# Qt WebEngine, so it runs in any configuration.
qt_add_executable(tst_web
    tst_web.cpp
)

target_link_libraries(tst_web PRIVATE
    awb_web
    awb_core
    awb_theme
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_web COMMAND tst_web)
