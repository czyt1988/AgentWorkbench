#ifndef AWB_WEB_WEBENGINEPROFILESTORE_H
#define AWB_WEB_WEBENGINEPROFILESTORE_H

#include <QHash>
#include <QObject>
#include <QString>

class QWebEngineProfile;

namespace awb::web {

// One persistent QWebEngineProfile per agent:
// storageName = "awb-<agentId>", storagePath = WebProfilePaths::profileDir
// — cookies and localStorage survive restarts, and two agents on the same
// host but different ports never share a cookie jar.
//
// Exposed to QML as the `WebProfiles` singleton (call createProfile).
class WebEngineProfileStore : public QObject
{
    Q_OBJECT

public:
    explicit WebEngineProfileStore(QObject *parent = nullptr);

    // Returns the cached profile for the agent, creating it on first use.
    Q_INVOKABLE QWebEngineProfile *createProfile(const QString &agentId);

    // Drop every profile (application shutdown).
    void shutdown();

private:
    QHash<QString, QWebEngineProfile *> m_profiles;
};

} // namespace awb::web

#endif // AWB_WEB_WEBENGINEPROFILESTORE_H
