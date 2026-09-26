#ifndef AWB_WORKBENCH_ENVIRONMENTSERVICE_H
#define AWB_WORKBENCH_ENVIRONMENTSERVICE_H

#include <QObject>
#include <QString>

namespace awb::core {
class ScriptRunner;
} // namespace awb::core

namespace awb::workbench {

// Python / Node.js detection for the status bar (01-architecture.md §4.8).
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

signals:
    void changed();

private:
    void detect(const QString &program, const QString &runtimeName);

    core::ScriptRunner *m_runner;
    QString m_pythonVersion;
    bool m_pythonInstalled = false;
    QString m_nodeVersion;
    bool m_nodeInstalled = false;
    int m_pending = 0;
    bool m_detecting = false;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_ENVIRONMENTSERVICE_H
