#ifndef AWB_WEB_WEBTABSFACADE_H
#define AWB_WEB_WEBTABSFACADE_H

#include <QAbstractItemModel>
#include <QUrl>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::web {

class WebSurfaceRegistry;
class WebTab;
class WebTabsModel;

// The QML facade for the web feature (01-architecture.md §4.5): tab
// lifecycle, surface selection and the memory policy (freeze inactive,
// LRU-release past maxLiveTabs — 02 §6.5). Strategy comes from
// Settings::webOptions().
class WebTabsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QString activeTabId READ activeTabId NOTIFY activeTabChanged)
    Q_PROPERTY(bool devToolsEnabled READ devToolsEnabled CONSTANT)
    Q_PROPERTY(bool freezeInactiveTabs READ freezeInactiveTabs NOTIFY
                   policyChanged)
    Q_PROPERTY(QString downloadDir READ downloadDir NOTIFY policyChanged)
    Q_PROPERTY(bool engineAvailable READ engineAvailable CONSTANT)

public:
    WebTabsFacade(core::Settings *settings, QObject *parent = nullptr);

    QAbstractItemModel *model() const;
    WebTabsModel *tabs() const { return m_tabs; }

    QString activeTabId() const;

    // True in Debug builds — the tab menu only offers devtools then.
    bool devToolsEnabled() const;

    bool freezeInactiveTabs() const;
    QString downloadDir() const;
    // Whether the embedded surface exists (false for AWB_ENABLE_WEBENGINE
    // =OFF builds — the settings page greys the option out).
    bool engineAvailable() const;

    // Like openTab, but ALWAYS opens a new tab — used for loopback popups
    // (OAuth windows, target=_blank on the same agent) where the same-agent
    // dedup of openTab would be wrong (02 §6.4).
    Q_INVOKABLE QString openDetachedTab(const QString &agentId,
                                        const QString &url,
                                        const QString &title);

    // Open a tab for {agentId, url, title, icon, color}. A tab for the same
    // agent already open is activated instead of duplicated. With the
    // `external` surface this opens the system browser and creates no tab
    // (02 §6.7). Returns the tab id (empty for the external path).
    Q_INVOKABLE QString openTab(const QVariantMap &fields);
    Q_INVOKABLE void closeTab(const QString &id);
    Q_INVOKABLE void activateTab(const QString &id);
    // Cycle the active tab (Ctrl+Tab); wraps around.
    Q_INVOKABLE void stepActiveTab(int delta);
    Q_INVOKABLE void reloadTab(const QString &id);
    Q_INVOKABLE void openExternal(const QString &id);
    // Restore a released view (state -> loading, surface reloads).
    Q_INVOKABLE void reopen(const QString &id);

    Q_INVOKABLE QVariantMap tabForAgent(const QString &agentId) const;
    // QML component URL for a surface kind ("" when unavailable).
    Q_INVOKABLE QString surfaceUrl(const QString &kind) const;

    // --- Report-backs from surfaces / health -----------------------------
    Q_INVOKABLE void setTabState(const QString &id, const QString &state);
    Q_INVOKABLE void setTabProgress(const QString &id, int progress);
    Q_INVOKABLE void setTabTitle(const QString &id, const QString &title);
    Q_INVOKABLE void setTabLastError(const QString &id, const QString &error);
    Q_INVOKABLE void setTabZoom(const QString &id, double zoom);
    Q_INVOKABLE void setTabUrl(const QString &id, const QString &url);

    // Cross-domain rules wired by BuiltinPages (01 §4.8).
    void markOfflineForAgent(const QString &agentId);
    void markOnlineForAgent(const QString &agentId);
    void closeTabsForAgent(const QString &agentId);

    // Register a surface component (awb_web_webengine calls this for
    // "embedded" at startup).
    void registerSurface(const QString &kind, const QString &componentUrl);

signals:
    void activeTabChanged();
    // web.freezeInactiveTabs / web.downloadDir changed in settings.json.
    void policyChanged();
    // Info-level notice for the external-surface path (02 §6.7); the
    // workbench turns it into a toast (wired in BuiltinPages).
    void externalOpened(const QString &url);

private:
    QString createTab(const QString &agentId, const QUrl &url,
                      const QVariantMap &fields);
    void wireActiveTracking();
    void applyMemoryPolicy();
    WebTab *tabForId(const QString &id) const;

    core::Settings *m_settings;
    WebTabsModel *m_tabs;
    class WebSurfaceRegistry *m_registry;
    int m_nextTabId = 1;
};

} // namespace awb::web

#endif // AWB_WEB_WEBTABSFACADE_H
