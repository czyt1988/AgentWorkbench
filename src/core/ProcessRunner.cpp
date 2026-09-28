#include "core/ProcessRunner.h"

#include <QProcess>
#include <QStandardPaths>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
// QTextCodec 在 Qt 5 属 QtCore、在 Qt 6 被移去 Core5Compat，decodeOutput
// 的两个版本各用各的解码器。
#include <QTextCodec>
#endif

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

QStringList ProcessRunner::splitCommand(const QString &command)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QProcess::splitCommand(command);
#else
    // 与 QProcess::splitCommand 同一套规则：空白分词、双引号成组、
    // 反斜杠仅转义引号自身、成对的引号字面量（"" -> "）。奇数引号这类
    // 非法输入 Qt 6 返回空表，这里保持一致。
    QStringList tokens;
    QString current;
    bool inQuote = false;
    bool tokenStarted = false;
    bool escaped = false;
    int quoteCount = 0;
    const int size = command.size();
    for (int i = 0; i < size; ++i) {
        const QChar c = command.at(i);
        if (escaped) {
            current += c;
            escaped = false;
        } else if (c == QLatin1Char('\\') && inQuote
                   && i + 1 < size
                   && command.at(i + 1) == QLatin1Char('"')) {
            escaped = true;
        } else if (c == QLatin1Char('"')) {
            ++quoteCount;
            inQuote = !inQuote;
            tokenStarted = true;
        } else if (!inQuote && c.isSpace()) {
            if (tokenStarted) {
                tokens.append(current);
                current.clear();
                tokenStarted = false;
            }
        } else {
            current += c;
            tokenStarted = true;
        }
    }
    if (quoteCount % 2 != 0)
        return {};
    if (tokenStarted)
        tokens.append(current);
    return tokens;
#endif
}

QString ProcessRunner::decodeOutput(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString result = decoder.decode(data);
    if (!decoder.hasError())
        return result;
#else
    // QTextDecoder::toUnicode 带 ConverterState，invalidChars 记录非法序列数，
    // 与 QStringDecoder::hasError 的判据等价。
    QTextCodec::ConverterState state;
    const QString result = QTextCodec::codecForName("UTF-8")
                               ->toUnicode(data.constData(), data.size(),
                                           &state);
    if (state.invalidChars == 0)
        return result;
#endif
    return QString::fromLocal8Bit(data);
}

} // namespace awb::core
