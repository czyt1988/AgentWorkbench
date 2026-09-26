#ifndef LOGGER_H
#define LOGGER_H

#include <QFile>
#include <QString>
#include <QStringList>

// Rotating-file log handler. Installs a Qt message handler that writes all
// qDebug/qInfo/qWarning/qCritical output to
//   ~/.AgentWorkbench/log/agentworkbench.log
// When that file reaches the size limit it is rotated to
// agentworkbench.log.1, the previous .1 becomes .2, and the oldest backup is
// deleted, so at most maxFiles files (current + backups) exist at any time.
class Logger
{
public:
    // Rotation policy: 5 MB per file, 3 files (current + 2 backups) → 15 MB.
    static constexpr qint64 DEFAULT_MAX_FILE_SIZE = 5 * 1024 * 1024;
    static constexpr int DEFAULT_MAX_FILES = 3;

    // Cap for the verbatim output of one command in the log.
    static constexpr int DEFAULT_MAX_OUTPUT = 16 * 1024;

    // Create the log directory, open the log file, and install the message
    // handler. Call once at startup, before any qWarning/etc.
    // `directory` overrides the default log directory; `maxFileSize` and
    // `maxFiles` override the rotation policy. Production uses the defaults —
    // the parameters exist so tests can exercise rotation without writing
    // megabytes.
    static void install(const QString &directory = QString(),
                        qint64 maxFileSize = DEFAULT_MAX_FILE_SIZE,
                        int maxFiles = DEFAULT_MAX_FILES);

    // Flush, close, and restore the default message handler. Mainly for tests,
    // which must not keep a message handler installed past the test case.
    static void uninstall();

    // Absolute path of the current log file, for messages that point the
    // user at the log. Empty when install() has not run yet.
    static QString logFilePath();

    // Render a program and its arguments as one copy-pasteable command line,
    // double-quoting arguments that contain whitespace or quotes. Used by the
    // "[cmd]" log lines so the log shows what was really executed.
    static QString formatCommandLine(const QString &program,
                                     const QStringList &args = QStringList());

    // Cap a captured command output at `limit` characters, appending a note
    // when text was dropped, so one chatty command cannot fill the log.
    static QString clampOutput(const QString &text,
                               int limit = DEFAULT_MAX_OUTPUT);

private:
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &msg);

    // Close and rotate the current log file if it has grown too large.
    static void rotateIfNeeded();

    // Path of backup #index (1 = most recent, maxFiles - 1 = oldest).
    static QString backupPath(int index);

    static QFile s_logFile;
    static QString s_logPath;
    static qint64 s_maxFileSize;
    static int s_maxFiles;
    // Bytes written to the current file, tracked so rotation does not have to
    // stat the file after every message.
    static qint64 s_bytesWritten;
};

#endif // LOGGER_H
