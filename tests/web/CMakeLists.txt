# tst_web — web tabs suite. Deliberately does NOT link
# Qt WebEngine, so it runs in any configuration.
add_executable(tst_web
    tst_web.cpp
)

target_link_libraries(tst_web PRIVATE
    awb_web
    awb_core
    awb_theme
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Test
)

add_test(NAME tst_web COMMAND tst_web)
