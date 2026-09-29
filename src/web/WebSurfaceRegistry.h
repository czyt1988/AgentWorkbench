#ifndef AWB_WEB_WEBSURFACEREGISTRY_H
#define AWB_WEB_WEBSURFACEREGISTRY_H

#include <QHash>
#include <QObject>
#include <QString>

namespace awb::web {

/// Web 表面（surface）的 kind → QML 组件 URL 注册表。
///
/// kind 标识一种呈现方式：`external` 由 awb_web 自己注册，永远存在；
/// `embedded` 由 awb_web_webengine 在构建包含 WebEngine 时注册。
class WebSurfaceRegistry : public QObject
{
    Q_OBJECT

public:
    explicit WebSurfaceRegistry(QObject *parent = nullptr);

    // 登记或覆盖一个 kind 的组件 URL；kind 为空时忽略
    void registerSurface(const QString &kind, const QString &componentUrl);
    // 移除一个 kind；不存在时无效果
    void unregisterSurface(const QString &kind);

    // 该 kind 的 QML 组件 URL；kind 未注册时返回空串
    Q_INVOKABLE QString surfaceUrl(const QString &kind) const;
    // 该 kind 是否已注册
    Q_INVOKABLE bool hasSurface(const QString &kind) const;

Q_SIGNALS:
    /**
     * @brief 已注册的表面集合发生变化时发射
     *
     * 登记新 kind 或注销已有 kind 都会触发，界面据此重新评估可用的
     * 呈现方式。
     */
    void surfacesChanged();

private:
    QHash<QString, QString> m_surfaces;  ///< kind -> QML 组件 URL
};

} // namespace awb::web

#endif // AWB_WEB_WEBSURFACEREGISTRY_H
