#ifndef AWB_CORE_PROCESSRUNNER_H
#define AWB_CORE_PROCESSRUNNER_H

#include <QByteArray>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace awb::core {

// Outcome of a synchronous ProcessRunner::run().
struct ProcessResult
{
    bool started = false; // the process launched (even if it then timed out)
    int exitCode = -1;
    QString stdOut;
    QString stdErr;
    QString error; // non-empty when the process did not start or timed out
};

// Mechanism layer for external commands. No
// business knowledge: which command to run and what its output means stays
// with the calling module.
class ProcessRunner
{
public:
    // Resolve a bare program name through PATH, applying PATHEXT on Windows
    // so npm-style .cmd/.bat shims ("qwen" -> "qwen.cmd") are found.
    // Empty when not found.
    static QString findExecutable(const QString &program);

    // Start `program args` detached (survives this application exiting) and
    // report its PID. Pass an already-resolved absolute program for the
    // Windows .cmd/.bat wrapping decisions to stay with the caller.
    // When `outputFile` is non-empty the child's stdout AND stderr are
    // redirected there (truncated on each start) — the capture that lets
    // the agents domain pick session URLs out of the agent's own console
    // output (dsh prints its per-process token URL there).
    // On failure returns false and fills `error`.
    static bool startDetached(const QString &program, const QStringList &args,
                              qint64 *pid = nullptr, QString *error = nullptr,
                              const QString &workingDirectory = QString(),
                              const QProcessEnvironment &env = QProcessEnvironment(),
                              const QString &outputFile = QString());

    // Run to completion, capturing both channels. `timeoutMs` <= 0 waits
    // forever; on timeout the process is killed, error says so and
    // exitCode stays -1.
    static ProcessResult run(const QString &program, const QStringList &args,
                             int timeoutMs = 30000);

    // Kill a whole process tree (Windows: taskkill /F /T /PID — covers the
    // cmd -> qwen.cmd -> node chain). Fire-and-forget like 0.3.0: returns
    // false only when the kill command itself could not be launched.
    static bool killTree(qint64 pid);

    // The kill command killTree() runs, split for logging: callers log the
    // real thing before executing it.
    static QString killProgram();
    static QStringList killProgramArgs(qint64 pid);

    // Decode bytes captured from a child process (npm/node/PowerShell etc.).
    // Modern CLI tools emit UTF-8; invalid sequences fall back to the system
    // locale codec (e.g. GBK on zh-CN Windows) so legacy cmd output still
    // decodes instead of becoming mojibake.
    static QString decodeOutput(const QByteArray &data);
};

} // namespace awb::core

#endif // AWB_CORE_PROCESSRUNNER_H
