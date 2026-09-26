#include "web/webengine/WebEngineProfileStore.h"

#include "web/WebProfilePaths.h"

#include <QWebEngineProfile>

namespace awb::web {

WebEngineProfileStore::WebEngineProfileStore(QObject *parent)
    : QObject(parent)
{
}

QWebEngineProfile *WebEngineProfileStore::createProfile(const QString &agentId)
{
    const auto it = m_profiles.constFind(agentId);
    if (it != m_profiles.constEnd())
        return it.value();

    // Qt 6.7 fixes the profile name at construction (no setStorageName).
    auto *profile = new QWebEngineProfile(
        WebProfilePaths::storageName(agentId), this);
    profile->setPersistentStoragePath(WebProfilePaths::profileDir(agentId));
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile->setPersistentCookiesPolicy(
        QWebEngineProfile::ForcePersistentCookies);
    m_profiles.insert(agentId, profile);
    return profile;
}

void WebEngineProfileStore::shutdown()
{
    qDeleteAll(m_profiles);
    m_profiles.clear();
}

} // namespace awb::web
