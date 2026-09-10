#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

const qint64 MAX_SIZE = 10 * 1024 * 1024; // 10 MB
const int MAX_BACKUPS = 1;                // 1 backup → 2 files total

QFile Logger::s_logFile;
QString Logger::s_logPath;

void Logger::install()
{
    // Log directory: ~/.AgentLauncher/log/
    s_logPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                + QStringLiteral("/.AgentLauncher/log");
    QDir().mkpath(s_logPath);

    s_logFile.setFileName(s_logPath + QStringLiteral("/agentlauncher.log"));
    s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    qInstallMessageHandler(Logger::messageHandler);

    qInfo() << "AgentLauncher: logging started →"
            << (s_logPath + QStringLiteral("/agentlauncher.log"));
}

QString Logger::logFilePath()
{
    if (s_logPath.isEmpty())
        return {};
    return s_logPath + QStringLiteral("/agentlauncher.log");
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
        rotateIfNeeded();
    }

    // Mirror to stderr so console / debug output still works.
    fprintf(stderr, "%s\n", qUtf8Printable(line));
    fflush(stderr);
}

void Logger::rotateIfNeeded()
{
    if (s_logFile.size() < MAX_SIZE)
        return;

    s_logFile.close();

    const QString current = s_logPath + QStringLiteral("/agentlauncher.log");
    const QString backup  = s_logPath + QStringLiteral("/agentlauncher.log.1");

    // Remove the old backup, then promote the current file to backup.
    QFile::remove(backup);
    QFile::rename(current, backup);

    // Open a fresh current file.
    s_logFile.setFileName(current);
    s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}
