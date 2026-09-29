#include "web/WebSurfaceRegistry.h"

namespace awb::web {

WebSurfaceRegistry::WebSurfaceRegistry(QObject *parent)
    : QObject(parent)
{
    // The external surface always exists: it hands the URL to the system
    // browser and the page degrades to a list of running agents
    m_surfaces.insert(QStringLiteral("external"), QString());
}

void WebSurfaceRegistry::registerSurface(const QString &kind,
                                         const QString &componentUrl)
{
    if (kind.isEmpty()) {
        return;
    }
    m_surfaces.insert(kind, componentUrl);
    Q_EMIT surfacesChanged();
}

void WebSurfaceRegistry::unregisterSurface(const QString &kind)
{
    if (m_surfaces.remove(kind) > 0) {
        Q_EMIT surfacesChanged();
    }
}

QString WebSurfaceRegistry::surfaceUrl(const QString &kind) const
{
    return m_surfaces.value(kind);
}

bool WebSurfaceRegistry::hasSurface(const QString &kind) const
{
    return m_surfaces.contains(kind);
}

} // namespace awb::web
