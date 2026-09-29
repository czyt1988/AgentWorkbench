#include "web/WebSurfaceRegistry.h"

namespace awb::web {

/**
 * @brief 构造表面注册表
 *
 * @param parent QObject 父项
 */
WebSurfaceRegistry::WebSurfaceRegistry(QObject *parent)
    : QObject(parent)
{
    // external 表面永远存在：它没有 QML 组件，语义是把 URL 交给系统
    // 浏览器打开；缺少嵌入表面时页面据此退化为运行中 agent 的列表。
    m_surfaces.insert(QStringLiteral("external"), QString());
}

/**
 * @brief 登记一个表面 kind
 *
 * 同名 kind 重复登记时覆盖旧值。集合因此变化时发射 surfacesChanged()。
 *
 * @param kind 表面标识；空串被忽略（也不发信号）
 * @param componentUrl 该表面页面的 QML 组件 URL；external 形态登记为空串
 */
void WebSurfaceRegistry::registerSurface(const QString &kind,
                                         const QString &componentUrl)
{
    if (kind.isEmpty()) {
        return;
    }
    m_surfaces.insert(kind, componentUrl);
    Q_EMIT surfacesChanged();
}

/**
 * @brief 注销一个表面 kind
 *
 * @param kind 表面标识；不存在时无效果（也不发信号）
 */
void WebSurfaceRegistry::unregisterSurface(const QString &kind)
{
    if (m_surfaces.remove(kind) > 0) {
        Q_EMIT surfacesChanged();
    }
}

/**
 * @brief 查询一个 kind 的 QML 组件 URL
 *
 * @param kind 表面标识
 * @return 组件 URL；kind 未注册（或为 external 的空 URL 形态）时返回空串
 */
QString WebSurfaceRegistry::surfaceUrl(const QString &kind) const
{
    return m_surfaces.value(kind);
}

/**
 * @brief 查询一个 kind 是否已登记
 *
 * @param kind 表面标识
 * @return 已登记时返回 true
 */
bool WebSurfaceRegistry::hasSurface(const QString &kind) const
{
    return m_surfaces.contains(kind);
}

} // namespace awb::web
