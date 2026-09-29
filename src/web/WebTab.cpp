#include "web/WebTab.h"

namespace awb::web {

/**
 * @brief 构造一个标签
 *
 * 初始状态为 loading；构造即 touch()——新标签在 LRU 释放策略里算刚用过。
 *
 * @param id 标签 id（facade 侧生成）
 * @param agentId 所属 agent 的 id
 * @param url 初始 URL
 * @param title 显示标题
 * @param iconSource 图标（agent 定义）
 * @param color agent 颜色
 * @param surfaceKind 呈现方式 kind
 * @param parent QObject 父项
 */
WebTab::WebTab(const QString &id, const QString &agentId, const QUrl &url,
               const QString &title, const QString &iconSource,
               const QString &color, const QString &surfaceKind,
               QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_agentId(agentId)
    , m_url(url)
    , m_title(title)
    , m_iconSource(iconSource)
    , m_color(color)
    , m_surfaceKind(surfaceKind)
{
    touch();
}

/**
 * @brief 写入 URL
 *
 * 值未变化时不发信号，表面的 url 绑定因此不会空转重新求值。
 *
 * @param url 新 URL
 */
void WebTab::setUrl(const QUrl &url)
{
    if (m_url == url) {
        return;
    }
    m_url = url;
    Q_EMIT urlChanged();
}

/**
 * @brief 写入显示标题
 *
 * 值未变化时不发信号。
 *
 * @param title 新标题
 */
void WebTab::setTitle(const QString &title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    Q_EMIT titleChanged();
}

/**
 * @brief 推进状态机
 *
 * 值未变化时不发信号；进入 loading 会驱动表面的 reload，进入 released
 * 令表面销毁视图（联动关系见 WebTabsFacade.cpp 与 WebEngineSurface.qml）。
 *
 * @param state 新状态，取值见类说明（loading | ready | offline | crashed |
 *              error | released）
 */
void WebTab::setState(const QString &state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
}

/**
 * @brief 写入加载进度
 *
 * 先夹到 [0, 100]；夹后与旧值相同则不发信号。
 *
 * @param progress 任意整数，越界值按边界取
 */
void WebTab::setLoadProgress(int progress)
{
    progress = qBound(0, progress, 100);
    if (m_loadProgress == progress) {
        return;
    }
    m_loadProgress = progress;
    Q_EMIT loadProgressChanged();
}

/**
 * @brief 写入最近一次错误说明
 *
 * 值未变化时不发信号。
 *
 * @param error 可直接展示的文案；空串表示清除
 */
void WebTab::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}

/**
 * @brief 写入缩放系数
 *
 * 先夹到 [0.5, 2.0]；与旧值模糊相等（qFuzzyCompare）则不发信号，
 * 微小浮点差不当作变化。
 *
 * @param zoom 任意倍率，越界值按边界取
 */
void WebTab::setZoom(double zoom)
{
    zoom = qBound(0.5, zoom, 2.0);
    if (qFuzzyCompare(m_zoom, zoom)) {
        return;
    }
    m_zoom = zoom;
    Q_EMIT zoomChanged();
}

} // namespace awb::web
