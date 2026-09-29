#include "web/webengine/WebEngineProfileStore.h"

#include "web/WebProfilePaths.h"

#include <QQuickWebEngineProfile>

namespace awb::web {

WebEngineProfileStore::WebEngineProfileStore(QObject *parent)
    : QObject(parent)
{
}

QQuickWebEngineProfile *WebEngineProfileStore::createProfile(const QString &agentId)
{
    const auto it = m_profiles.constFind(agentId);
    if (it != m_profiles.constEnd()) {
        return it.value();
    }

    // Unlike the core QWebEngineProfile (name fixed at construction), the
    // Quick profile still has setStorageName — either way both properties
    // must be set before the first view uses the profile, which is exactly
    // what happens here.
    auto *profile = new QQuickWebEngineProfile(this);
    profile->setStorageName(WebProfilePaths::storageName(agentId));
    profile->setPersistentStoragePath(WebProfilePaths::profileDir(agentId));
    // The public ctor builds the adapter with an empty name, and the adapter
    // constructor decides off-the-record from exactly that — setStorageName
    // does NOT flip it. Without this reset the profile silently stays
    // off-the-record: no cookies on disk, sessions lost on every restart
    // (verified against Qt 6.7.3).
    profile->setOffTheRecord(false);
    profile->setHttpCacheType(QQuickWebEngineProfile::DiskHttpCache);
    profile->setPersistentCookiesPolicy(
        QQuickWebEngineProfile::ForcePersistentCookies);
    m_profiles.insert(agentId, profile);
    return profile;
}

void WebEngineProfileStore::shutdown()
{
    qDeleteAll(m_profiles);
    m_profiles.clear();
}

} // namespace awb::web
