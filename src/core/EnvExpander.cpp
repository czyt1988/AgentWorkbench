#include "core/EnvExpander.h"

#include <QRegularExpression>
#include <QStandardPaths>

namespace awb::core {

/**
 * @brief 展开路径中的环境变量与波浪号
 *
 * @param path 原始路径，可含 %VAR%（如 %USERPROFILE%）与前导 "~"
 * @return 展开后的路径；未知的 %VAR% 原样保留，"~/" 替换为 home 目录
 */
QString EnvExpander::expand(const QString &path)
{
    QString result = path;
    // %VAR% 风格变量是 Windows 习惯（如 %USERPROFILE%），整串扫一遍再拼回。
    static const QRegularExpression re(QStringLiteral("%(\\w+)%"));
    QRegularExpressionMatchIterator it = re.globalMatch(result);
    QString out;
    int cursor = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += result.mid(cursor, m.capturedStart() - cursor);
        const QString var = m.captured(1);
        const QString val = qEnvironmentVariable(qUtf8Printable(var));
        out += val.isEmpty() ? m.captured(0) : val;
        cursor = m.capturedEnd();
    }
    out += result.mid(cursor);
    // "~/" 是 unix 习惯的便利写法，同样支持。
    out.replace(QStringLiteral("~/"),
                QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                    + QStringLiteral("/"));
    return out;
}

} // namespace awb::core
