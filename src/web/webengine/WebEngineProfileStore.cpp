#include "web/webengine/WebEngineProfileStore.h"

#include "web/WebProfilePaths.h"

#include <QQuickWebEngineProfile>

namespace awb::web {

/**
 * @brief 构造 profile 存储
 *
 * @param parent QObject 父项
 */
WebEngineProfileStore::WebEngineProfileStore(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 取（必要时创建）该 agent 的持久 profile
 *
 * 返回类型必须是 QQuickWebEngineProfile：view 的 profile 属性收的就是
 * 这个类型，QML 也调不了返回类型未注册的方法——返回 QWebEngineProfile*
 * 时 QML 报 "Unknown method return type"，所有嵌入标签全部落在共享的
 * 默认 profile 上。新建的 profile 设磁盘 HTTP 缓存与强制持久 cookie，
 * 路径与名字按 agent 划分（见 WebProfilePaths）。
 *
 * @param agentId agent id
 * @return 该 agent 的 profile；同一 agent 恒返回同一实例（本类持有它）
 */
QQuickWebEngineProfile *WebEngineProfileStore::createProfile(const QString &agentId)
{
    const auto it = m_profiles.constFind(agentId);
    if (it != m_profiles.constEnd()) {
        return it.value();
    }

    // 与 core 的 QWebEngineProfile（名字构造时定死）不同，Quick 版仍有
    // setStorageName——但无论哪版，这两个属性都必须在第一个视图用上
    // profile 之前设好，这里正是创建时机。
    auto *profile = new QQuickWebEngineProfile(this);
    profile->setStorageName(WebProfilePaths::storageName(agentId));
    profile->setPersistentStoragePath(WebProfilePaths::profileDir(agentId));
    // 公开构造函数用空名字建 adapter，而 adapter 构造时恰恰据此判定为
    // 隐身——setStorageName 不会翻转它。漏了这条重置，profile 会静默
    // 保持 off-the-record：cookie 不落盘，每次重启丢会话（在 Qt 6.7.3
    // 上实测过）。
    profile->setOffTheRecord(false);
    profile->setHttpCacheType(QQuickWebEngineProfile::DiskHttpCache);
    profile->setPersistentCookiesPolicy(
        QQuickWebEngineProfile::ForcePersistentCookies);
    m_profiles.insert(agentId, profile);
    return profile;
}

/**
 * @brief 丢弃全部缓存的 profile
 *
 * 应用退出时调用：释放 profile 对象并清空缓存；之后再调
 * createProfile 会重新创建。
 */
void WebEngineProfileStore::shutdown()
{
    qDeleteAll(m_profiles);
    m_profiles.clear();
}

} // namespace awb::web
