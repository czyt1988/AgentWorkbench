#ifndef LOGGER_H
#define LOGGER_H

#include <QFile>
#include <QString>

// Rotating-file log handler. Installs a Qt message handler that writes
// all qDebug/qInfo/qWarning/qCritical output to
//   ~/.AgentLauncher/log/agentlauncher.log
// When the file reaches 10 MB it is rotated to agentlauncher.log.1
// (replacing any previous backup), keeping at most 2 files on disk.
class Logger
{
public:
    // Create the log directory, open the log file, and install the
    // message handler. Call once at startup, before any qWarning/etc.
    static void install();

    // Absolute path of the current log file, for messages that point the
    // user at the log. Empty when install() has not run yet.
    static QString logFilePath();

private:
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &msg);

    // Close and rotate the current log file if it has grown too large.
    static void rotateIfNeeded();

    static QFile s_logFile;
    static QString s_logPath;
};

#endif // LOGGER_H
