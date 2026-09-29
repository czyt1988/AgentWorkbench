#include "core/TextUtils.h"

#include <QRegularExpression>

namespace awb::core {

namespace {

/**
 * @brief 给命令行参数加引号
 *
 * 只有裸拼会改变读法（含空白）或粘回 cmd.exe 会出歧义（含引号）的参数才加引号，
 * 其余原样返回，保证日志可读。
 *
 * @param arg 单个参数
 * @return 加引号后的参数；空参数返回空引号对 ""
 */
QString quoteArg(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    static const QRegularExpression needsQuoting(QStringLiteral("[\\s\"]"));
    if (!needsQuoting.match(arg).hasMatch()) {
        return arg;
    }
    QString quoted = arg;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(quoted);
}

} // namespace

/**
 * @brief 从命令输出中提取版本号
 *
 * 匹配 x.y.z（可带预发布后缀），如 "1.2.3"、"v1.2.3-beta"、"1.2.3.4"。
 *
 * @param output 命令的完整输出
 * @return 第一个匹配的版本串；没有匹配时返回空串
 */
QString TextUtils::extractVersion(const QString &output)
{
    static const QRegularExpression re(
        QStringLiteral("(\\d+\\.\\d+\\.\\d+[\\w.-]*)"));
    const auto match = re.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
}

/**
 * @brief 把程序与参数渲染成一条命令行
 *
 * 每个参数经 quoteArg() 处理后用空格拼接，日志里的 "[cmd]" 行用它呈现
 * 真正执行的内容，可以直接复制回终端。
 *
 * @param program 程序名
 * @param args 参数列表
 * @return 可复制粘贴的命令行
 */
QString TextUtils::formatCommandLine(const QString &program,
                                     const QStringList &args)
{
    QStringList parts;
    parts.reserve(args.size() + 1);
    parts << quoteArg(program);
    for (const QString &arg : args) {
        parts << quoteArg(arg);
    }
    return parts.join(QLatin1Char(' '));
}

/**
 * @brief 截断过长的命令输出
 *
 * @param text 原始输出
 * @param limit 保留的字符上限；非正数表示不截断
 * @return 超长时截断到 limit 并追加 "(N more characters not logged)" 提示
 */
QString TextUtils::clampOutput(const QString &text, int limit)
{
    if (limit <= 0 || text.size() <= limit) {
        return text;
    }
    return text.left(limit)
           + QStringLiteral("\n… (%1 more characters not logged)")
                 .arg(text.size() - limit);
}

} // namespace awb::core
