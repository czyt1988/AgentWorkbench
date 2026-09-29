#include "workbench/EnvironmentService.h"

#include "core/ProcessRunner.h"
#include "core/ScriptRunner.h"
#include "core/TextUtils.h"

namespace awb::workbench {

EnvironmentService::EnvironmentService(QObject *parent)
    : QObject(parent)
    , m_runner(new core::ScriptRunner(this))
{
    connect(m_runner, &core::ScriptRunner::finished, this,
            [this](const QString &key, bool ok, int exitCode,
                   const QString &stdOut, const QString &stdErr,
                   const QString &error) {
                const QString runtime = key.section(QLatin1Char(':'), -1);
                const bool isPython = (runtime == QStringLiteral("Python"));

                QString version;
                if (error.isEmpty()) {
                    version = core::TextUtils::extractVersion(stdOut);
                    if (version.isEmpty()) {
                        version = core::TextUtils::extractVersion(stdErr);
                    }
                }
                const bool installed =
                    error.isEmpty() && (exitCode == 0 || !version.isEmpty());
                if (isPython) {
                    m_pythonInstalled = installed;
                    m_pythonVersion = installed ? version : QString();
                } else {
                    m_nodeInstalled = installed;
                    m_nodeVersion = installed ? version : QString();
                }

                if (m_inflight.remove(key)) {
                    m_detecting = !m_inflight.isEmpty();
                }
                Q_EMIT changed();
            });

    refresh();
}

void EnvironmentService::refresh()
{
    detect(QStringLiteral("python"), QStringLiteral("Python"));
    detect(QStringLiteral("node"), QStringLiteral("Node"));
}

void EnvironmentService::detect(const QString &program,
                                const QString &runtimeName)
{
    if (core::ProcessRunner::findExecutable(program).isEmpty()) {
        if (runtimeName == QStringLiteral("Python")) {
            m_pythonInstalled = false;
            m_pythonVersion.clear();
        } else {
            m_nodeInstalled = false;
            m_nodeVersion.clear();
        }
        Q_EMIT changed();
        return;
    }

    const QString key = QStringLiteral("environment:") + runtimeName;
    // Insert (not count): a refresh while this probe is in flight starts a
    // superseding run under the same key; its finished() clears it once.
    m_inflight.insert(key);
    m_detecting = true;
    Q_EMIT changed();
    // Separate channels (older Python prints the version to stderr).
    m_runner->runShell(key, program + QStringLiteral(" --version"), 10000,
                       false);
}

} // namespace awb::workbench
