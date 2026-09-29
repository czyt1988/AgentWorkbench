#ifndef AWB_WORKBENCH_ENVIRONMENTSERVICE_H
#define AWB_WORKBENCH_ENVIRONMENTSERVICE_H

#include <QObject>
#include <QSet>
#include <QString>

namespace awb::core {
class ScriptRunner;
} // namespace awb::core

namespace awb::workbench {

// Python / Node.js detection for the status bar.
// QML global name: `environment` (registered in main.cpp).
class EnvironmentService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString pythonVersion READ pythonVersion NOTIFY changed)
    Q_PROPERTY(bool pythonInstalled READ pythonInstalled NOTIFY changed)
    Q_PROPERTY(QString nodeVersion READ nodeVersion NOTIFY changed)
    Q_PROPERTY(bool nodeInstalled READ nodeInstalled NOTIFY changed)
    Q_PROPERTY(bool detecting READ detecting NOTIFY changed)

public:
    explicit EnvironmentService(QObject *parent = nullptr);

    QString pythonVersion() const { return m_pythonVersion; }
    bool pythonInstalled() const { return m_pythonInstalled; }
    QString nodeVersion() const { return m_nodeVersion; }
    bool nodeInstalled() const { return m_nodeInstalled; }
    bool detecting() const { return m_detecting; }

    // Re-run both probes (status bar refresh button).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void changed();

private:
    void detect(const QString &program, const QString &runtimeName);

    core::ScriptRunner *m_runner;
    QString m_pythonVersion;
    bool m_pythonInstalled = false;
    QString m_nodeVersion;
    bool m_nodeInstalled = false;
    // In-flight probe keys ("environment:Python" / "environment:Node").
    // A plain counter leaked: refresh() while a probe runs supersedes it
    // (ScriptRunner's epoch drops the stale finished()), so the counter
    // never returned to zero and `detecting` stuck true. A keyed set is
    // idempotent — the superseding run removes the same key when it ends.
    QSet<QString> m_inflight;
    bool m_detecting = false;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_ENVIRONMENTSERVICE_H
