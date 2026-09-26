#ifndef AWB_WEB_WEBSURFACEREGISTRY_H
#define AWB_WEB_WEBSURFACEREGISTRY_H

#include <QHash>
#include <QObject>
#include <QString>

namespace awb::web {

// kind -> QML component URL (01-architecture.md §4.5). `external` is
// registered by awb_web itself (it always exists); `embedded` is registered
// by awb_web_webengine when the build includes WebEngine.
class WebSurfaceRegistry : public QObject
{
    Q_OBJECT

public:
    explicit WebSurfaceRegistry(QObject *parent = nullptr);

    void registerSurface(const QString &kind, const QString &componentUrl);
    void unregisterSurface(const QString &kind);

    // QML component URL for a kind; empty when the kind is unknown.
    Q_INVOKABLE QString surfaceUrl(const QString &kind) const;
    Q_INVOKABLE bool hasSurface(const QString &kind) const;

signals:
    void surfacesChanged();

private:
    QHash<QString, QString> m_surfaces;
};

} // namespace awb::web

#endif // AWB_WEB_WEBSURFACEREGISTRY_H
