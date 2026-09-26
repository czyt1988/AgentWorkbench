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
                const bool isPython = (runtime == QLatin1String("Python"));

                QString version;
                if (error.isEmpty()) {
                    version = core::TextUtils::extractVersion(stdOut);
                    if (version.isEmpty())
                        version = core::TextUtils::extractVersion(stdErr);
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

                if (m_pending > 0)
                    --m_pending;
                m_detecting = m_pending > 0;
                emit changed();
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
        if (runtimeName == QLatin1String("Python")) {
            m_pythonInstalled = false;
            m_pythonVersion.clear();
        } else {
            m_nodeInstalled = false;
            m_nodeVersion.clear();
        }
        emit changed();
        return;
    }

    ++m_pending;
    m_detecting = true;
    emit changed();
    // Separate channels (older Python prints the version to stderr).
    m_runner->runShell(QStringLiteral("environment:") + runtimeName,
                       program + QStringLiteral(" --version"), 10000, false);
}

} // namespace awb::workbench
