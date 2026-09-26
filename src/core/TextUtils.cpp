#include "core/TextUtils.h"

#include <QRegularExpression>

namespace awb::core {

namespace {

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

QString TextUtils::extractVersion(const QString &output)
{
    // Match x.y.z (optionally with a pre-release suffix), e.g. "1.2.3",
    // "v1.2.3-beta", "1.2.3.4".
    static const QRegularExpression re(
        QStringLiteral("(\\d+\\.\\d+\\.\\d+[\\w.-]*)"));
    const auto match = re.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString TextUtils::formatCommandLine(const QString &program,
                                     const QStringList &args)
{
    QStringList parts;
    parts.reserve(args.size() + 1);
    parts << quoteArg(program);
    for (const QString &arg : args)
        parts << quoteArg(arg);
    return parts.join(QLatin1Char(' '));
}

QString TextUtils::clampOutput(const QString &text, int limit)
{
    if (limit <= 0 || text.size() <= limit)
        return text;
    return text.left(limit)
           + QStringLiteral("\n… (%1 more characters not logged)")
                 .arg(text.size() - limit);
}

} // namespace awb::core
