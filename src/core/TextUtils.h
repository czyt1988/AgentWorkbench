#ifndef AWB_CORE_TEXTUTILS_H
#define AWB_CORE_TEXTUTILS_H

#include <QString>
#include <QStringList>

namespace awb::core {

// Small text helpers shared across modules (01-architecture.md §4.1).
class TextUtils
{
public:
    // Extract an x.y.z version string (optionally with a pre-release suffix,
    // e.g. "v1.2.3-beta", "1.2.3.4") from command output. Empty when none.
    static QString extractVersion(const QString &output);

    // Render a program and its arguments as one copy-pasteable command line,
    // double-quoting arguments that contain whitespace or quotes. Used by
    // the "[cmd]" log lines so the log shows what was really executed.
    static QString formatCommandLine(const QString &program,
                                     const QStringList &args = QStringList());

    // Cap a captured command output at `limit` characters, appending a note
    // when text was dropped, so one chatty command cannot fill the log.
    static QString clampOutput(const QString &text, int limit);
};

} // namespace awb::core

#endif // AWB_CORE_TEXTUTILS_H
