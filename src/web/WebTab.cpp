#include "web/WebTab.h"

namespace awb::web {

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

void WebTab::setUrl(const QUrl &url)
{
    if (m_url == url)
        return;
    m_url = url;
    emit urlChanged();
}

void WebTab::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit titleChanged();
}

void WebTab::setState(const QString &state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void WebTab::setLoadProgress(int progress)
{
    progress = qBound(0, progress, 100);
    if (m_loadProgress == progress)
        return;
    m_loadProgress = progress;
    emit loadProgressChanged();
}

void WebTab::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void WebTab::setZoom(double zoom)
{
    zoom = qBound(0.5, zoom, 2.0);
    if (qFuzzyCompare(m_zoom, zoom))
        return;
    m_zoom = zoom;
    emit zoomChanged();
}

} // namespace awb::web
