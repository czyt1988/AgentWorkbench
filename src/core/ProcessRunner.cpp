#include "core/ProcessRunner.h"

#include <QProcess>
#include <QStandardPaths>
#include <QStringDecoder>

namespace awb::core {

QString ProcessRunner::findExecutable(const QString &program)
{
    if (program.isEmpty())
        return QString();
    // findExecutable searches PATH and, on Windows, appends the PATHEXT
    // extensions (.exe/.cmd/.bat/...), which is exactly what is needed to
    // resolve npm-style shims like "qwen" -> "qwen.cmd".
    return QStandardPaths::findExecutable(program);
}

bool ProcessRunner::startDetached(const QString &program, const QStringList &args,
                                  qint64 *pid, QString *error,
                                  const QString &workingDirectory,
                                  const QProcessEnvironment &env,
                                  const QString &outputFile)
{
    if (program.isEmpty()) {
        if (error)
            *error = QStringLiteral("program is empty");
        return false;
    }

    // Resolve a bare program through PATH (PATHEXT on Windows) so npm-style
    // .cmd/.bat shims (e.g. "qwen" -> "qwen.cmd") are found. CreateProcess on
    // its own does not try those extensions.
    const QString resolved = findExecutable(program);
    if (resolved.isEmpty()) {
        if (error)
            *error = QStringLiteral("'%1' is not on PATH").arg(program);
        return false;
    }

    QProcess proc;
    proc.setProgram(resolved);
    proc.setArguments(args);
    if (!workingDirectory.isEmpty())
        proc.setWorkingDirectory(workingDirectory);
    if (!env.isEmpty())
        proc.setProcessEnvironment(env);
    if (!outputFile.isEmpty()) {
        // The member startDetached() honors channel setup (the static
        // overload does not): merge stderr into stdout and redirect both.
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.setStandardOutputFile(outputFile, QIODevice::Truncate);
    }

    qint64 outPid = 0;
    const bool ok = proc.startDetached(&outPid);
    if (ok && pid)
        *pid = outPid;
    if (!ok && error)
        *error = proc.errorString();
    return ok;
}

ProcessResult ProcessRunner::run(const QString &program, const QStringList &args,
                                 int timeoutMs)
{
    ProcessResult result;
    if (program.isEmpty()) {
        result.error = QStringLiteral("program is empty");
        return result;
    }

    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(timeoutMs > 0 ? timeoutMs : 30000)) {
        result.error = QStringLiteral("failed to start: %1").arg(proc.errorString());
        return result;
    }
    result.started = true;

    if (timeoutMs <= 0) {
        proc.waitForFinished(-1);
    } else if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(3000);
        result.error = QStringLiteral("timed out after %1 ms").arg(timeoutMs);
        result.stdOut = decodeOutput(proc.readAllStandardOutput());
        result.stdErr = decodeOutput(proc.readAllStandardError());
        return result;
    }

    result.exitCode = proc.exitCode();
    result.stdOut = decodeOutput(proc.readAllStandardOutput());
    result.stdErr = decodeOutput(proc.readAllStandardError());
    return result;
}

QString ProcessRunner::killProgram()
{
#ifdef Q_OS_WIN
    return QStringLiteral("taskkill");
#else
    return QStringLiteral("kill");
#endif
}

QStringList ProcessRunner::killProgramArgs(qint64 pid)
{
#ifdef Q_OS_WIN
    // /F force, /T kills the whole process tree (cmd -> qwen.cmd -> node).
    return {QStringLiteral("/F"), QStringLiteral("/T"),
            QStringLiteral("/PID"), QString::number(pid)};
#else
    return {QStringLiteral("-9"), QString::number(pid)};
#endif
}

bool ProcessRunner::killTree(qint64 pid)
{
    qint64 outPid = 0;
    return startDetached(killProgram(), killProgramArgs(pid), &outPid);
}

QString ProcessRunner::decodeOutput(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString result = decoder.decode(data);
    if (!decoder.hasError())
        return result;
    return QString::fromLocal8Bit(data);
}

} // namespace awb::core
