// The smallest possible AgentWorkbench plugin (specs/01 §9.4): it exports
// the two C entry points and registers one page through the SAME
// registration path the built-in pages use (§9.1).
//
// Build with -DAWB_BUILD_PLUGIN_EXAMPLES=ON; the build copies the DLL and
// plugin.json into <dataRoot>/plugins/hello/, where the host discovers it.
// Enable it in Settings -> Plugins (restart required).

#include "plugin_api/PluginApi.h"

namespace {

// Host services handed to us at registration time.
awb::plugin::Services *g_services = nullptr;

void logInfo(const QString &message)
{
    if (g_services)
        g_services->log(0, message);
}

} // namespace

extern "C" {

AWB_PLUGIN_EXPORT int awb_plugin_api_version()
{
    return awb::plugin::ApiVersion;
}

AWB_PLUGIN_EXPORT int awb_plugin_register(awb::plugin::Services *services)
{
    if (!services)
        return 1;
    g_services = services;

    awb::plugin::PageDescriptor page;
    page.id = QStringLiteral("hello");
    page.title = QStringLiteral("Hello");
    page.icon = QStringLiteral("qrc:/icons/bot.svg");
    page.source = QStringLiteral("qrc:/hello/HelloPage.qml");
    page.section = QStringLiteral("extensions");
    page.order = 50;
    services->registerPage(page);

    // A page contributed by a plugin must not be able to crash the host:
    // everything goes through the Services interface.
    logInfo(QStringLiteral("hello plugin registered its page"));
    return 0;
}

} // extern "C"
