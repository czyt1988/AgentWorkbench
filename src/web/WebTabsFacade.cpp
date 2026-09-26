#include "web/WebTabsFacade.h"

#include "core/Paths.h"
#include "core/Settings.h"
#include "web/WebSurfaceRegistry.h"
#include "web/WebTab.h"
#include "web/WebTabsModel.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>

namespace awb::web {

WebTabsFacade::WebTabsFacade(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_tabs(new WebTabsModel(this))
    , m_registry(new WebSurfaceRegistry(this))
{
    wireActiveTracking();

    connect(settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QLatin1String("web.freezeInactiveTabs")
                    || key == QLatin1String("web.downloadDir"))
                    emit policyChanged();
            });
}

QAbstractItemModel *WebTabsFacade::model() const
{
    return m_tabs;
}

QString WebTabsFacade::activeTabId() const
{
    return m_tabs->activeTabId();
}

bool WebTabsFacade::devToolsEnabled() const
{
#ifdef QT_DEBUG
    return true;
#else
    return false;
#endif
}

WebTab *WebTabsFacade::tabForId(const QString &id) const
{
    return m_tabs->tabById(id);
}

void WebTabsFacade::registerSurface(const QString &kind,
                                    const QString &componentUrl)
{
    m_registry->registerSurface(kind, componentUrl);
}

QString WebTabsFacade::surfaceUrl(const QString &kind) const
{
    return m_registry->surfaceUrl(kind);
}

QVariantMap WebTabsFacade::tabForAgent(const QString &agentId) const
{
    QVariantMap map;
    const WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab)
        return map;
    map[QStringLiteral("id")] = tab->id();
    map[QStringLiteral("agentId")] = tab->agentId();
    map[QStringLiteral("url")] = tab->url().toString();
    map[QStringLiteral("title")] = tab->title();
    map[QStringLiteral("state")] = tab->state();
    return map;
}

QString WebTabsFacade::openTab(const QVariantMap &fields)
{
    const QString agentId = fields.value(QStringLiteral("agentId")).toString();
    const QUrl url(fields.value(QStringLiteral("url")).toString());
    if (!url.isValid() || url.isEmpty())
        return {};

    // Surface policy: `external` hands the URL to the system browser and
    // creates no tab (02 §6.7); `embedded` needs the WebEngine surface to
    // be registered, otherwise it degrades to external as well.
    QString kind = m_settings->webOptions().surface;
    if (kind == QLatin1String("embedded")
        && !m_registry->hasSurface(QStringLiteral("embedded"))) {
        qWarning().noquote() << QStringLiteral(
            "WebTabs: the embedded surface is not available (built without "
            "WebEngine?); falling back to the system browser");
        kind = QStringLiteral("external");
    }
    if (kind == QLatin1String("external")) {
        if (QDesktopServices::openUrl(url))
            emit externalOpened(url.toString());
        else
            qWarning().noquote() << QStringLiteral(
                "WebTabs: no handler accepted %1").arg(url.toString());
        return {};
    }

    // Same agent already open -> activate instead of duplicating (S5-T1).
    if (WebTab *existing = m_tabs->tabForAgent(agentId)) {
        activateTab(existing->id());
        return existing->id();
    }
    return createTab(agentId, url, fields);
}

QString WebTabsFacade::createTab(const QString &agentId, const QUrl &url,
                                 const QVariantMap &fields)
{
    const QString id = QStringLiteral("tab-%1").arg(m_nextTabId++);
    auto *tab = new WebTab(id, agentId, url,
                           fields.value(QStringLiteral("title")).toString(),
                           fields.value(QStringLiteral("icon")).toString(),
                           fields.value(QStringLiteral("color")).toString(),
                           QStringLiteral("embedded"), this);
    m_tabs->appendTab(tab);
    m_tabs->setActiveIndex(m_tabs->rowCount() - 1);
    tab->touch();
    applyMemoryPolicy();
    return id;
}

QString WebTabsFacade::openDetachedTab(const QString &agentId,
                                       const QString &url,
                                       const QString &title)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.isEmpty())
        return {};
    if (m_settings->webOptions().surface != QLatin1String("embedded")
        || !m_registry->hasSurface(QStringLiteral("embedded")))
        return {};
    QVariantMap fields;
    fields[QStringLiteral("title")] = title;
    return createTab(agentId, parsed, fields);
}

void WebTabsFacade::closeTab(const QString &id)
{
    // Closing destroys the view — the agent process keeps running
    // (02 §6.2); session data survives in the per-agent profile.
    if (m_tabs->activeTabId() == id) {
        const int row = m_tabs->rowOfTab(id);
        m_tabs->removeTab(id);
        if (m_tabs->rowCount() > 0)
            m_tabs->setActiveIndex(qMin(row, m_tabs->rowCount() - 1));
    } else {
        m_tabs->removeTab(id);
    }
    emit activeTabChanged();
}

void WebTabsFacade::activateTab(const QString &id)
{
    const int row = m_tabs->rowOfTab(id);
    if (row < 0)
        return;
    m_tabs->setActiveIndex(row);
    emit activeTabChanged();
}

void WebTabsFacade::stepActiveTab(int delta)
{
    const int count = m_tabs->rowCount();
    if (count == 0)
        return;
    int index = m_tabs->activeIndex();
    if (index < 0)
        index = 0;
    else
        index = (index + delta + count) % count;
    m_tabs->setActiveIndex(index);
    emit activeTabChanged();
}

void WebTabsFacade::reloadTab(const QString &id)
{
    WebTab *tab = tabForId(id);
    if (!tab)
        return;
    if (tab->state() == QLatin1String("released")) {
        reopen(id);
        return;
    }
    // The surface watches state transitions: loading triggers (re)load.
    tab->setState(QStringLiteral("loading"));
    tab->setLoadProgress(0);
}

void WebTabsFacade::openExternal(const QString &id)
{
    WebTab *tab = tabForId(id);
    if (!tab)
        return;
    // The escape hatch must work even when the embedded view is broken
    // (02 §6.7): strip the token fragment from the log line only.
    QDesktopServices::openUrl(tab->url());
}

void WebTabsFacade::reopen(const QString &id)
{
    WebTab *tab = tabForId(id);
    if (!tab)
        return;
    tab->setLoadProgress(0);
    tab->setState(QStringLiteral("loading")); // view re-creates and loads
}

void WebTabsFacade::setTabState(const QString &id, const QString &state)
{
    WebTab *tab = tabForId(id);
    if (!tab)
        return;
    tab->setState(state);
    if (state == QLatin1String("ready"))
        tab->touch();
}

void WebTabsFacade::setTabProgress(const QString &id, int progress)
{
    if (WebTab *tab = tabForId(id))
        tab->setLoadProgress(progress);
}

void WebTabsFacade::setTabTitle(const QString &id, const QString &title)
{
    if (WebTab *tab = tabForId(id))
        tab->setTitle(title);
}

void WebTabsFacade::setTabLastError(const QString &id, const QString &error)
{
    if (WebTab *tab = tabForId(id))
        tab->setLastError(error);
}

void WebTabsFacade::setTabZoom(const QString &id, double zoom)
{
    if (WebTab *tab = tabForId(id))
        tab->setZoom(zoom);
}

void WebTabsFacade::setTabUrl(const QString &id, const QString &url)
{
    if (WebTab *tab = tabForId(id))
        tab->setUrl(QUrl(url));
}

void WebTabsFacade::markOfflineForAgent(const QString &agentId)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab)
        return;
    if (tab->state() == QLatin1String("ready")
        || tab->state() == QLatin1String("loading"))
        tab->setState(QStringLiteral("offline"));
}

void WebTabsFacade::markOnlineForAgent(const QString &agentId)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab)
        return;
    // ready/offline/error/crashed + agent back -> loading (02 §6.3).
    // Released tabs stay released until the user restores them.
    const QString state = tab->state();
    if (state == QLatin1String("ready")
        || state == QLatin1String("offline")
        || state == QLatin1String("error")
        || state == QLatin1String("crashed")) {
        tab->setLoadProgress(0);
        tab->setState(QStringLiteral("loading"));
    }
}

void WebTabsFacade::closeTabsForAgent(const QString &agentId)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (tab)
        closeTab(tab->id());
}

bool WebTabsFacade::freezeInactiveTabs() const
{
    return m_settings->webOptions().freezeInactiveTabs;
}

QString WebTabsFacade::downloadDir() const
{
    const QString configured = m_settings->webOptions().downloadDir;
    if (!configured.isEmpty())
        return configured;
    return core::Paths::downloadsDir();
}

bool WebTabsFacade::engineAvailable() const
{
    return m_registry->hasSurface(QStringLiteral("embedded"));
}

void WebTabsFacade::wireActiveTracking()
{
    connect(m_tabs, &WebTabsModel::activeIndexChanged, this,
            &WebTabsFacade::activeTabChanged);
}

// LRU release: past maxLiveTabs, the least recently used INACTIVE tab is
// released (view destroyed, tab kept — 02 §6.5). Only tabs that currently
// own a view count as live.
void WebTabsFacade::applyMemoryPolicy()
{
    if (!m_settings->webOptions().freezeInactiveTabs)
        return;
    const int maxLive = m_settings->webOptions().maxLiveTabs;

    QList<WebTab *> live;
    for (int i = 0; i < m_tabs->rowCount(); ++i) {
        WebTab *tab = m_tabs->tabAt(i);
        const QString state = tab->state();
        if (state != QLatin1String("released") && tab->id() != activeTabId())
            live.append(tab);
    }
    while (live.size() > qMax(1, maxLive)) {
        // Oldest by lastUsedMs first (the active tab is never in this list).
        WebTab *oldest = nullptr;
        for (WebTab *tab : live) {
            if (!oldest || tab->lastUsedMs() < oldest->lastUsedMs())
                oldest = tab;
        }
        if (!oldest)
            break;
        oldest->setState(QStringLiteral("released"));
        live.removeAll(oldest);
    }
}

} // namespace awb::web
