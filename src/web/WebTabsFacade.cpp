#include "web/WebTabsFacade.h"

#include "core/Paths.h"
#include "core/Settings.h"
#include "web/WebSurfaceRegistry.h"
#include "web/WebTab.h"
#include "web/WebTabsModel.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QStringList>
#include <QUrl>

namespace awb::web {

namespace {
// Display form of a tab URL: the bearer token never reaches a log line or a
// user-visible toast (it must stay out of anything a third party can read).
// Both spellings are redacted: the #token=… fragment (qwen) and the
// ?token=… query (dsh session URLs).
QString redactedUrl(const QUrl &url)
{
    QString text = url.toString(QUrl::RemoveFragment);

    // Drop the token= query item, keep the rest of the query.
    const int queryStart = text.indexOf(QLatin1Char('?'));
    if (queryStart >= 0) {
        QStringList kept;
        for (const QString &part : text.mid(queryStart + 1).split(QLatin1Char('&'))) {
            if (part.startsWith(QLatin1String("token=")))
                continue;
            kept.append(part);
        }
        text = text.left(queryStart);
        if (!kept.isEmpty())
            text += QLatin1Char('?') + kept.join(QLatin1Char('&'));
    }

    const QString fragment = url.fragment();
    if (fragment.isEmpty())
        return text;

    // Drop only the token= part; keep any legitimate fragment text.
    QStringList parts;
    bool droppedToken = false;
    for (const QString &part : fragment.split(QLatin1Char('&'))) {
        if (part.startsWith(QLatin1String("token="))) {
            droppedToken = true;
            continue;
        }
        parts.append(part);
    }
    if (!droppedToken)
        text += QLatin1Char('#') + fragment;
    else if (!parts.isEmpty())
        text += QLatin1Char('#') + parts.join(QLatin1Char('&'));
    return text;
}
} // namespace

WebTabsFacade::WebTabsFacade(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_tabs(new WebTabsModel(this))
    , m_registry(new WebSurfaceRegistry(this))
{
    wireActiveTracking();

    // Drive the notifiable tabCount off the model's structural changes —
    // QML bindings can't depend on rowCount(), which has no NOTIFY.
    connect(m_tabs, &QAbstractItemModel::rowsInserted, this,
            &WebTabsFacade::tabCountChanged);
    connect(m_tabs, &QAbstractItemModel::rowsRemoved, this,
            &WebTabsFacade::tabCountChanged);

    connect(settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QLatin1String("web.freezeInactiveTabs")
                    || key == QLatin1String("web.downloadDir"))
                    emit policyChanged();
                // Lowering the cap must take effect immediately, not only
                // on the next tab open.
                else if (key == QLatin1String("web.maxLiveTabs"))
                    applyMemoryPolicy();
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

int WebTabsFacade::tabCount() const
{
    return m_tabs->rowCount();
}

QString WebTabsFacade::activeState() const
{
    const WebTab *tab = tabForId(activeTabId());
    return tab ? tab->state() : QString();
}

QObject *WebTabsFacade::tabObject(const QString &id) const
{
    // Live QObject* so QML property reads (zoom, state …) stay reactive.
    return tabForId(id);
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
    // creates no tab; `embedded` needs the WebEngine surface to
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
        if (QDesktopServices::openUrl(url)) {
            qInfo().noquote() << QStringLiteral(
                "WebTabs: opened %1 in the system browser")
                                     .arg(redactedUrl(url));
            emit externalOpened(redactedUrl(url));
        } else {
            qWarning().noquote() << QStringLiteral(
                "WebTabs: no handler accepted %1").arg(redactedUrl(url));
        }
        return {};
    }

    // Same agent already open -> activate instead of duplicating.
    if (WebTab *existing = m_tabs->tabForAgent(agentId)) {
        activateTab(existing->id());
        qInfo().noquote() << QStringLiteral(
            "WebTabs: activated the existing tab %1 for %2")
            .arg(existing->id(), agentId);
        return existing->id();
    }

    const QString id = createTab(agentId, url, fields);
    qInfo().noquote() << QStringLiteral(
        "WebTabs: opened tab %1 for agent %2 (%3, surface=%4)")
        .arg(id, agentId, redactedUrl(url), kind);
    return id;
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
    // session data survives in the per-agent profile.
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
    // The escape hatch must work even when the embedded view is broken.
    // The browser needs the token fragment; the log line must
    // not have it.
    qInfo().noquote() << QStringLiteral(
        "WebTabs: opened %1 in the system browser")
                             .arg(redactedUrl(tab->url()));
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
        || tab->state() == QLatin1String("loading")
        // An error page observed while the agent is down is really an
        // "agent offline" page: recovery then goes through the normal
        // offline -> loading path. A load error with the agent STILL
        // running (HTTP 401 from a token gate) stays an error — reloading
        // it every probe round could never succeed.
        || tab->state() == QLatin1String("error"))
        tab->setState(QStringLiteral("offline"));
}

void WebTabsFacade::markOnlineForAgent(const QString &agentId)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab)
        return;
    // offline + agent back -> loading. Released tabs stay released until
    // the user restores them. Error/crashed are NOT auto-reloaded: the
    // agent is up (the health probe passed), so the load itself failed —
    // only a retarget or a manual Retry can change that outcome.
    if (tab->state() == QLatin1String("offline")) {
        tab->setLoadProgress(0);
        tab->setState(QStringLiteral("loading"));
    }
}

void WebTabsFacade::retargetTabForAgent(const QString &agentId, const QUrl &url)
{
    WebTab *tab = m_tabs->tabForAgent(agentId);
    if (!tab || url.isEmpty() || !url.isValid())
        return;
    tab->setLoadProgress(0);
    tab->setLastError(QString());
    tab->setUrl(url);
    if (tab->state() != QLatin1String("loading"))
        tab->setState(QStringLiteral("loading"));
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
    connect(m_tabs, &WebTabsModel::activeIndexChanged, this,
            &WebTabsFacade::activeStateChanged);
    // Any dataChanged on the active row means one of its properties moved
    // (state, title, progress …) — re-read activeState in QML.
    connect(m_tabs, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &,
                   const QVector<int> &) {
                if (topLeft.row() == m_tabs->activeIndex())
                    emit activeStateChanged();
            });
}

// LRU release: past maxLiveTabs, the least recently used INACTIVE tab is
// released (view destroyed, tab kept). The active tab's view
// counts toward the cap too — "视图上限" bounds ALL live views, so the
// loop stops at maxLive live views in total, not maxLive + 1.
// Not gated on freezeInactiveTabs: the freeze setting only controls the
// CPU trade-off of switched-away views, while this cap is the memory
// bound and must hold either way.
void WebTabsFacade::applyMemoryPolicy()
{
    const int maxLive = qMax(1, m_settings->webOptions().maxLiveTabs);

    QList<WebTab *> releasable; // live but inactive — candidates
    int liveCount = 0;          // every view that currently exists
    for (int i = 0; i < m_tabs->rowCount(); ++i) {
        WebTab *tab = m_tabs->tabAt(i);
        if (tab->state() == QLatin1String("released"))
            continue;
        ++liveCount;
        if (tab->id() != activeTabId())
            releasable.append(tab);
    }
    while (liveCount > maxLive && !releasable.isEmpty()) {
        // Oldest by lastUsedMs first (the active tab is never released).
        WebTab *oldest = nullptr;
        for (WebTab *tab : releasable) {
            if (!oldest || tab->lastUsedMs() < oldest->lastUsedMs())
                oldest = tab;
        }
        if (!oldest)
            break;
        oldest->setState(QStringLiteral("released"));
        releasable.removeAll(oldest);
        --liveCount;
    }
}

} // namespace awb::web
