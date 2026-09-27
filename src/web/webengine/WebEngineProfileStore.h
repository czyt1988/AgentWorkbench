#ifndef AWB_WEB_WEBENGINEPROFILESTORE_H
#define AWB_WEB_WEBENGINEPROFILESTORE_H

#include <QHash>
#include <QObject>
#include <QString>

class QQuickWebEngineProfile;

namespace awb::web {

// One persistent QQuickWebEngineProfile per agent:
// storageName = "awb-<agentId>", storagePath = WebProfilePaths::profileDir
// — cookies and localStorage survive restarts, and two agents on the same
// host but different ports never share a cookie jar.
//
// The type must be QQuickWebEngineProfile (the WebEngineProfile QML type):
// the view's `profile` property takes a QQuickWebEngineProfile*, and QML
// cannot even call a method whose return type is unregistered — a
// QWebEngineProfile* return produced "Unknown method return type" and left
// every embedded tab on the shared default profile.
//
// Exposed to QML as the `WebProfiles` singleton (call createProfile).
class WebEngineProfileStore : public QObject
{
    Q_OBJECT

public:
    explicit WebEngineProfileStore(QObject *parent = nullptr);

    // Returns the cached profile for the agent, creating it on first use.
    Q_INVOKABLE QQuickWebEngineProfile *createProfile(const QString &agentId);

    // Drop every profile (application shutdown).
    void shutdown();

private:
    QHash<QString, QQuickWebEngineProfile *> m_profiles;
};

} // namespace awb::web

#endif // AWB_WEB_WEBENGINEPROFILESTORE_H
