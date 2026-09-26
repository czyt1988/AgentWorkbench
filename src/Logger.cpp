#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {

const QString kLogFileName = QStringLiteral("agentworkbench.log");

// Quote an argument only when leaving it bare would change how the line reads
// (whitespace) or how it could be pasted back into cmd.exe (quotes).
QString quoteArg(const QString &arg)
{
    if (arg.isEmpty())
        return QStringLiteral("\"\"");
    static const QRegularExpression needsQuoting(QStringLiteral("[\\s\"]"));
    if (!needsQuoting.match(arg).hasMatch())
        return arg;
    QString quoted = arg;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(quoted);
}

} // namespace

QFile Logger::s_logFile;
QString Logger::s_logPath;
qint64 Logger::s_maxFileSize = Logger::DEFAULT_MAX_FILE_SIZE;
int Logger::s_maxFiles = Logger::DEFAULT_MAX_FILES;
qint64 Logger::s_bytesWritten = 0;

void Logger::install(const QString &directory, qint64 maxFileSize, int maxFiles)
{
    s_maxFileSize = maxFileSize > 0 ? maxFileSize : DEFAULT_MAX_FILE_SIZE;
    s_maxFiles = qMax(1, maxFiles);

    // Log directory: ~/.AgentWorkbench/log/ unless the caller overrides it.
    s_logPath = directory.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                          + QStringLiteral("/.AgentWorkbench/log")
                    : directory;
    QDir().mkpath(s_logPath);

    if (s_logFile.isOpen())
        s_logFile.close();
    s_logFile.setFileName(logFilePath());

    qInstallMessageHandler(Logger::messageHandler);

    // The handler still mirrors to stderr, so a log file that cannot be opened
    // (read-only data directory, disk full) is reported there instead of
    // silently losing every message.
    if (!s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning().noquote() << QStringLiteral("AgentWorkbench: cannot write the log "
                                               "file %1: %2")
                                    .arg(logFilePath(), s_logFile.errorString());
        return;
    }
    s_bytesWritten = s_logFile.size();

    qInfo().noquote() << QStringLiteral(
                             "AgentWorkbench: logging started → %1 "
                             "(rotating at %2 KB, keeping %3 files)")
                             .arg(logFilePath())
                             .arg(s_maxFileSize / 1024)
                             .arg(s_maxFiles);
}

void Logger::uninstall()
{
    qInstallMessageHandler(nullptr);

    if (s_logFile.isOpen()) {
        s_logFile.flush();
        s_logFile.close();
    }
    s_bytesWritten = 0;
}

QString Logger::logFilePath()
{
    if (s_logPath.isEmpty())
        return {};
    return s_logPath + QLatin1Char('/') + kLogFileName;
}

QString Logger::backupPath(int index)
{
    return QStringLiteral("%1.%2").arg(logFilePath()).arg(index);
}

QString Logger::formatCommandLine(const QString &program, const QStringList &args)
{
    QStringList parts;
    parts.reserve(args.size() + 1);
    parts << quoteArg(program);
    for (const QString &arg : args)
        parts << quoteArg(arg);
    return parts.join(QLatin1Char(' '));
}

QString Logger::clampOutput(const QString &text, int limit)
{
    if (limit <= 0 || text.size() <= limit)
        return text;
    return text.left(limit)
           + QStringLiteral("\n… (%1 more characters not logged)")
                 .arg(text.size() - limit);
}

void Logger::messageHandler(QtMsgType type,
                             const QMessageLogContext &context,
                             const QString &msg)
{
    // Level label
    const char *level = "DEBUG";
    switch (type) {
    case QtInfoMsg:     level = "INFO";    break;
    case QtWarningMsg:  level = "WARNING"; break;
    case QtCriticalMsg:  level = "CRITICAL"; break;
    case QtFatalMsg:     level = "FATAL";  break;
    case QtDebugMsg:
    default:            level = "DEBUG";   break;
    }

    const QString timestamp = QDateTime::currentDateTime()
                                  .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));

    // Include source location when available (file:line in the category).
    QString location;
    if (context.file)
        location = QStringLiteral(" [%1:%2]").arg(context.file).arg(context.line);

    const QString line = QStringLiteral("[%1] [%2]%3 %4")
                             .arg(timestamp)
                             .arg(QString::fromLatin1(level))
                             .arg(location)
                             .arg(msg);

    // Write to file
    if (s_logFile.isOpen()) {
        QTextStream ts(&s_logFile);
        ts << line << Qt::endl;
        ts.flush();
        // Approximate on purpose: the count only has to be good enough to
        // trigger rotation, and the file is never asked for its size.
        s_bytesWritten += line.toUtf8().size() + 1;
        rotateIfNeeded();
    }

    // Mirror to stderr so console / debug output still works.
    fprintf(stderr, "%s\n", qUtf8Printable(line));
    fflush(stderr);
}

void Logger::rotateIfNeeded()
{
    if (!s_logFile.isOpen() || s_bytesWritten < s_maxFileSize)
        return;

    s_logFile.close();

    if (s_maxFiles > 1) {
        // Drop the oldest backup, then shift the remaining ones up a slot:
        // .1 -> .2, …, current -> .1.
        QFile::remove(backupPath(s_maxFiles - 1));
        for (int i = s_maxFiles - 2; i >= 1; --i)
            QFile::rename(backupPath(i), backupPath(i + 1));
        QFile::rename(logFilePath(), backupPath(1));
    }

    // Reopen the current file. With a single allowed file there is nothing to
    // rotate to, so it is truncated instead.
    s_logFile.open(QIODevice::WriteOnly | QIODevice::Text
                   | (s_maxFiles > 1 ? QIODevice::Append : QIODevice::Truncate));
    s_bytesWritten = 0;
}
