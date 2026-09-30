#include "web/webengine/WebEngineProfileStore.h"

#include "web/WebProfilePaths.h"

#include <QQuickWebEngineProfile>

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
// 6.8+：QQuickWebEngineProfile 继承 core 的 QWebEngineProfile，profile 级
// scripts() 集合可用（公共头），polyfill 在这里按 profile 注入。6.2–6.7
// 没有引擎就绪前可用的 profile 级集合（见 createProfile 的注释），注入
// 挪到 view 级（WebEngineCompat::installCompatScript），本文件不参与。
#include "web/webengine/WebEngineCompat.h"

#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#endif

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
 * Qt 6.8+ 上还在这里给 profile 注入旧引擎兼容 polyfill（profile 级
 * scripts() 集合，一次注入、本 profile 全部视图生效）；6.2–6.7 的
 * QQuickWebEngineScriptCollection 要等 profile 关联上 QML engine 才可
 * 用——profile 在这里创建时没有任何视图用过它，此刻插入直接命中
 * collection 的 Q_ASSERT(engine)（6.7.3 上实测）——因此这两档的注入
 * 走 WebEngineCompat::installCompatScript 的 view 级路径；Qt 5 的 Quick
 * profile 没有任何 profile 级脚本集合，同走 view 级。
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // 旧引擎兼容 polyfill：MainWorld + DocumentCreation（早于页面任何
    // 脚本、页面脚本可见），子框架同注。源码全部带特性检测，Chromium
    // 118+ 上等于空转。profile 每个 agent 只创建一次，无需防重。
    QWebEngineScript script;
    script.setName(QStringLiteral("awb-compat-polyfills"));
    script.setSourceCode(WebEngineCompat::compatScriptSource());
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    profile->scripts()->insert(script);
#endif

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
