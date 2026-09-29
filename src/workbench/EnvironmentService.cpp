#include "workbench/EnvironmentService.h"

#include "core/ProcessRunner.h"
#include "core/ScriptRunner.h"
#include "core/TextUtils.h"

namespace awb::workbench {

/**
 * @brief 构造检测服务并做首次探测
 *
 * 接上 ScriptRunner 的 finished()（版本串优先从 stdout 提取，取不到再
 * 看 stderr；判安装的条件是命令成功或能提取到版本），随后立即探测
 * 两个运行时。
 *
 * @param parent QObject 父项
 */
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

/**
 * @brief 重跑两个探测（状态栏刷新按钮）
 *
 * 探测中重复调用是安全的：新运行顶掉同 key 的旧运行（见 m_inflight
 * 的说明），结果以最后出发的为准。
 */
void EnvironmentService::refresh()
{
    detect(QStringLiteral("python"), QStringLiteral("Python"));
    detect(QStringLiteral("node"), QStringLiteral("Node"));
}

/**
 * @brief 探测一个运行时
 *
 * PATH 上找不到可执行文件时直接置未安装并广播（不跑进程）；否则以
 * key "environment:<runtimeName>" 跑 `<program> --version`（10 s 超时，
 * stdout/stderr 分开收——老版本 Python 把版本打到 stderr）。
 *
 * @param program 可执行文件名（python / node）
 * @param runtimeName 显示用运行时名（Python / Node），也是探测 key 的尾巴
 */
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
    // 插 key 而不是计数：在途时再来一次 refresh() 会在同一 key 下顶掉
    // 旧运行，它的 finished() 只清一次。
    m_inflight.insert(key);
    m_detecting = true;
    Q_EMIT changed();
    // 通道分开：老版本 Python 把版本打到 stderr。
    m_runner->runShell(key, program + QStringLiteral(" --version"), 10000,
                       false);
}

} // namespace awb::workbench
