#include "tools/PromptTemplate.h"

#include "core/Logging.h"

#include <QDate>
#include <QRegularExpression>

namespace awb::tools {

namespace {

using PromptTemplateIo::kCategoryKey;
using PromptTemplateIo::kDescriptionKey;
using PromptTemplateIo::kIdKey;
using PromptTemplateIo::kTagsKey;
using PromptTemplateIo::kTitleKey;

/// 变量语法：`{{name}}` 或 `{{name:默认值}}`。名字限定 ASCII 标识符
/// （避免中文名带来的编码歧义）；默认值里允许除 `}` 外的任何字符，
/// 含空格与中文。
const QRegularExpression &variablePattern()
{
    static const QRegularExpression re(
        QStringLiteral("\\{\\{\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*(?::\\s*([^}]*))?\\s*\\}\\}"));
    return re;
}

} // namespace

/**
 * @brief 从模板文件文本解析出模板
 *
 * 头部是可选的：文件不以 `---` 开头就整篇当正文（title 由调用方兜底）。
 * 坏行（没有冒号的、键不认识的）跳过并记警告，不拖着整个文件失败——
 * 用户手改的文件，能救一行是一行。
 */
PromptTemplate PromptTemplateIo::parse(const QString &raw,
                                      const QString &fallbackId,
                                      PromptTemplate::Scope scope)
{
    PromptTemplate result;
    result.scope = scope;
    result.id = fallbackId;

    const QStringList lines = raw.split(QLatin1Char('\n'));
    int bodyStart = 0;
    if (!lines.isEmpty() && lines.constFirst().trimmed() == QLatin1String("---")) {
        bool headerClosed = false;
        for (int i = 1; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            if (line == QLatin1String("---")) {
                bodyStart = i + 1;
                headerClosed = true;
                break;
            }
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon <= 0) {
                qWarning() << "[tools] template header line skipped:" << line;
                continue;
            }
            const QString key = line.left(colon).trimmed().toLower();
            const QString value = line.mid(colon + 1).trimmed();
            if (key == QLatin1String(kTitleKey)) {
                result.title = value;
            } else if (key == QLatin1String(kCategoryKey)) {
                result.category = value;
            } else if (key == QLatin1String(kTagsKey)) {
                for (const QString &tag : value.split(QLatin1Char(','))) {
                    const QString trimmed = tag.trimmed();
                    if (!trimmed.isEmpty()) {
                        result.tags.append(trimmed);
                    }
                }
            } else if (key == QLatin1String(kDescriptionKey)) {
                result.description = value;
            } else if (key == QLatin1String(kIdKey)) {
                // 显式 id 优先于文件主名：用户想固定一个稳定标识（比如
                // 覆盖内置模板）时不用改文件名。
                if (!value.isEmpty()) {
                    result.id = value;
                }
            } else {
                qWarning() << "[tools] template header key skipped:" << key;
            }
        }
        if (!headerClosed) {
            // 头部开了没关：整篇退回正文，头部字段作废（半截头部比
            // 错误字段更可能是一次没写完的手改）。
            qWarning() << "[tools] template header not closed; treating the"
                       << "whole file as body:" << fallbackId;
            bodyStart = 0;
            result = PromptTemplate();
            result.scope = scope;
            result.id = fallbackId;
        }
    }

    // 正文：换行归一（CRLF 文件按 \n 拆出来的行尾残留 \r 去掉），
    // 起始空行去掉、结尾保留一个换行——插入时结尾必然接草稿原文。
    QStringList bodyLines;
    for (int i = bodyStart; i < lines.size(); ++i) {
        bodyLines.append(lines.at(i));
    }
    while (!bodyLines.isEmpty() && bodyLines.constFirst().isEmpty()) {
        bodyLines.removeFirst();
    }
    QString body = bodyLines.join(QLatin1Char('\n'));
    if (!body.isEmpty() && !body.endsWith(QLatin1Char('\n'))) {
        body += QLatin1Char('\n');
    }
    result.body = body;

    if (result.title.isEmpty()) {
        result.title = fallbackId;
    }
    if (result.category.isEmpty()) {
        result.category = QStringLiteral("My templates");
    }
    return result;
}

/**
 * @brief 把模板序列化回文件文本
 */
QString PromptTemplateIo::serialize(const PromptTemplate &template_)
{
    QStringList lines;
    lines << QStringLiteral("---");
    lines << QStringLiteral("%1: %2").arg(QLatin1String(kIdKey),
                                          template_.id);
    lines << QStringLiteral("%1: %2").arg(QLatin1String(kTitleKey),
                                          template_.title);
    if (!template_.category.isEmpty()) {
        lines << QStringLiteral("%1: %2").arg(QLatin1String(kCategoryKey),
                                              template_.category);
    }
    if (!template_.tags.isEmpty()) {
        lines << QStringLiteral("%1: %2").arg(QLatin1String(kTagsKey),
                                              template_.tags.join(
                                                  QStringLiteral(", ")));
    }
    if (!template_.description.isEmpty()) {
        lines << QStringLiteral("%1: %2").arg(QLatin1String(kDescriptionKey),
                                              template_.description);
    }
    lines << QStringLiteral("---");
    lines << QString();
    lines << template_.body;
    return lines.join(QLatin1Char('\n'));
}

/**
 * @brief 按出现顺序提取占位变量
 */
QList<PromptVariables::Variable> PromptVariables::extract(const QString &body)
{
    QList<Variable> result;
    QRegularExpressionMatchIterator it =
        variablePattern().globalMatch(body);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString name = match.captured(1);
        // 同名只取第一次出现的形状（默认值以第一次为准——模板作者通常
        // 在第一处给默认，后面引用）。
        bool known = false;
        for (const Variable &existing : std::as_const(result)) {
            if (existing.name == name) {
                known = true;
                break;
            }
        }
        if (!known) {
            Variable variable;
            variable.name = name;
            variable.defaultValue = match.captured(2).trimmed();
            result.append(variable);
        }
    }
    return result;
}

/**
 * @brief 内置变量的当前值
 *
 * workspace = 草稿绑定的当前工作区（未绑定为空串）；date = 本地日期
 * （ISO 形式，跨区域可排序）。
 */
QString PromptVariables::builtInValue(const QString &name,
                                      const QString &workspace)
{
    if (name == QLatin1String(kWorkspace)) {
        return workspace;
    }
    if (name == QLatin1String(kDate)) {
        return QDate::currentDate().toString(Qt::ISODate);
    }
    return QString();
}

/**
 * @brief 是否内置变量
 */
bool PromptVariables::isBuiltIn(const QString &name)
{
    return name == QLatin1String(kWorkspace) || name == QLatin1String(kDate);
}

/**
 * @brief 把填好的值套进正文
 *
 * 有值（含默认值兜底）就替换为纯文本；两者都空的**原样保留** `{{name}}`
 * ——「跳过」的语义就是留着待办，界面侧负责把它高亮成待办色。
 */
QString PromptVariables::apply(const QString &body, const QVariantMap &values)
{
    QString result;
    qsizetype lastEnd = 0;
    QRegularExpressionMatchIterator it =
        variablePattern().globalMatch(body);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += body.mid(lastEnd, match.capturedStart() - lastEnd);
        const QString name = match.captured(1);
        QString value = values.value(name).toString();
        if (value.isEmpty()) {
            value = match.captured(2).trimmed();
        }
        if (value.isEmpty()) {
            // 没填也没默认：原样保留（待办）。
            result += match.captured(0);
        }
        else {
            result += value;
        }
        lastEnd = match.capturedEnd();
    }
    result += body.mid(lastEnd);
    return result;
}

} // namespace awb::tools
