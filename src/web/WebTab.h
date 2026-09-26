#ifndef AWB_WEB_WEBTAB_H
#define AWB_WEB_WEBTAB_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

namespace awb::web {

// One open Web tab (01-architecture.md §4.5). All properties are NOTifiable
// so the tab bar and the surface rebind when the state machine advances
// (02-ui-specification.md §6.3).
class WebTab : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString agentId READ agentId CONSTANT)
    Q_PROPERTY(QUrl url READ url NOTIFY urlChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString iconSource READ iconSource NOTIFY iconSourceChanged)
    Q_PROPERTY(QString color READ color NOTIFY colorChanged)
    Q_PROPERTY(QString surfaceKind READ surfaceKind NOTIFY surfaceKindChanged)
    // loading | ready | offline | crashed | error | released
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY loadProgressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)

public:
    WebTab(const QString &id, const QString &agentId, const QUrl &url,
           const QString &title, const QString &iconSource,
           const QString &color, const QString &surfaceKind,
           QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString agentId() const { return m_agentId; }
    QUrl url() const { return m_url; }
    QString title() const { return m_title; }
    QString iconSource() const { return m_iconSource; }
    QString color() const { return m_color; }
    QString surfaceKind() const { return m_surfaceKind; }
    QString state() const { return m_state; }
    int loadProgress() const { return m_loadProgress; }
    QString lastError() const { return m_lastError; }
    double zoom() const { return m_zoom; }

    // Mutators — called by WebTabsFacade (surfaces report back through it).
    void setUrl(const QUrl &url);
    void setTitle(const QString &title);
    void setState(const QString &state);
    void setLoadProgress(int progress);
    void setLastError(const QString &error);
    void setZoom(double zoom);

    // Wall-clock of the last activation, for the LRU release policy.
    qint64 lastUsedMs() const { return m_lastUsedMs; }
    void touch() { m_lastUsedMs = QDateTime::currentMSecsSinceEpoch(); }

signals:
    void urlChanged();
    void titleChanged();
    void iconSourceChanged();
    void colorChanged();
    void surfaceKindChanged();
    void stateChanged();
    void loadProgressChanged();
    void lastErrorChanged();
    void zoomChanged();

private:
    QString m_id;
    QString m_agentId;
    QUrl m_url;
    QString m_title;
    QString m_iconSource;
    QString m_color;
    QString m_surfaceKind;
    QString m_state = QStringLiteral("loading");
    int m_loadProgress = 0;
    QString m_lastError;
    double m_zoom = 1.0;
    qint64 m_lastUsedMs = 0;
};

} // namespace awb::web

#endif // AWB_WEB_WEBTAB_H
