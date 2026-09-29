#ifndef AWB_WEB_WEBENGINEPROFILESTORE_H
#define AWB_WEB_WEBENGINEPROFILESTORE_H

#include <QHash>
#include <QObject>
#include <QString>

class QQuickWebEngineProfile;

namespace awb::web {

/// 每个 agent 一个持久 QQuickWebEngineProfile 的缓存与工厂。
///
/// storageName 取 WebProfilePaths::storageName（"awb-<agentId>"）、
/// persistentStoragePath 取 WebProfilePaths::profileDir——cookie 与
/// localStorage 落盘、重启不丢，同 host 不同端口的两个 agent 永远不共享
/// cookie jar。类型必须是 QQuickWebEngineProfile（WebEngineProfile 的
/// QML 类型）：view 的 profile 属性收的就是它，QML 也调不了返回类型
/// 未注册的方法——坑的细节见 WebEngineProfileStore.cpp。
///
/// 经 AgentWorkbench.App 注册为 QML 单例 `WebProfiles`，QML 调
/// createProfile。
class WebEngineProfileStore : public QObject
{
    Q_OBJECT

public:
    explicit WebEngineProfileStore(QObject *parent = nullptr);

    // 取该 agent 的 profile；首次调用时创建，之后恒返回同一实例
    Q_INVOKABLE QQuickWebEngineProfile *createProfile(const QString &agentId);

    // 丢弃全部 profile（应用退出时用）
    void shutdown();

private:
    QHash<QString, QQuickWebEngineProfile *> m_profiles;  ///< agentId -> profile
};

} // namespace awb::web

#endif // AWB_WEB_WEBENGINEPROFILESTORE_H
