#include "theme/ThemeLoader.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace awb::theme {

namespace {

/// 主题文件允许的顶层键；清单之外的键告警后忽略（见 warnUnknown）。
const QSet<QString> kTopLevelKeys = {
    QStringLiteral("id"),          QStringLiteral("name"),
    QStringLiteral("variant"),     QStringLiteral("author"),
    QStringLiteral("description"), QStringLiteral("colors"),
    QStringLiteral("metrics"),     QStringLiteral("fonts"),
    QStringLiteral("agentPalette"),
};

/// fonts 对象里允许的键（其余键告警后忽略）。
const QSet<QString> kFontKeys = {QStringLiteral("family"),
                                 QStringLiteral("monoFamily")};

/**
 * @brief 告警一个未知键并说明它被忽略
 *
 * @param where 位置描述：文件名，或 "colors.xxx" 这类带前缀的键名
 * @param key   未识别的键名
 */
void warnUnknown(const QString &where, const QString &key)
{
    qWarning().noquote() << QStringLiteral(
        "ThemeLoader: unknown key %1 in %2; ignoring it").arg(key, where);
}

} // namespace

/**
 * @brief 解析并校验一个主题 JSON 对象
 *
 * 规则见 ThemeLoader.h 的类注释。两点动机：缺失令牌从同 variant 的
 * baseline 补齐，第三方主题只需声明它想改的令牌；id 必须等于文件名——
 * id 是注册表与 settings.json 共用的主键，名实不符的文件比少一个主题
 * 更糟，整体跳过。
 *
 * @param json     已解析的顶层 JSON 对象
 * @param fileName 裸文件名（不含 .json），必须等于主题声明的 id
 * @param baseline 同 variant 的内置主题，做未知键过滤与缺失值兜底；
 *                 传无效 ThemeFile 表示解析内置主题本身（构造上完整，
 *                 无兜底）
 * @param out      解析结果；返回 false 时为无效 ThemeFile
 * @return 文件应被跳过时返回 false（缺 id/name/variant、variant 未知、
 *         id 与文件名不符）
 * @sa loadFile
 */
bool ThemeLoader::parse(const QJsonObject &json, const QString &fileName,
                        const ThemeFile &baseline, ThemeFile &out)
{
    out = ThemeFile();

    for (const QString &key : json.keys()) {
        if (!kTopLevelKeys.contains(key)) {
            warnUnknown(fileName, key);
        }
    }

    const QString id = json.value(QStringLiteral("id")).toString();
    const QString name = json.value(QStringLiteral("name")).toString();
    const QString variant = json.value(QStringLiteral("variant")).toString();

    // 必需字段：无法自表身份（id/name/variant）的文件整个跳过。
    if (id.isEmpty() || name.isEmpty()
        || (variant != QStringLiteral("dark")
            && variant != QStringLiteral("light"))) {
        qWarning().noquote() << QStringLiteral(
            "ThemeLoader: %1.json is missing id/name/variant (or an unknown "
            "variant); skipping the file").arg(fileName);
        return false;
    }
    if (id != fileName) {
        qWarning().noquote() << QStringLiteral(
            "ThemeLoader: %1.json declares id \"%2\" — the file name must "
            "equal the id; skipping the file").arg(fileName, id);
        return false;
    }

    out.id = id;
    out.name = name;
    out.variant = variant;

    // colors：未知令牌忽略，非法颜色回退 baseline，缺失令牌随后统一补。
    const QJsonObject colors = json.value(QStringLiteral("colors")).toObject();
    for (const QString &key : colors.keys()) {
        if (baseline.isValid() && !baseline.colors.contains(key)) {
            warnUnknown(fileName, QStringLiteral("colors.") + key);
            continue;
        }
        const QColor parsed(colors.value(key).toString());
        if (parsed.isValid()) {
            out.colors.insert(key, parsed);
        } else {
            qWarning().noquote() << QStringLiteral(
                "ThemeLoader: %1.json colors.%2 is not a valid color; using "
                "the baseline value").arg(fileName, key);
            if (baseline.isValid()) {
                out.colors.insert(key, baseline.colors.value(key));
            }
        }
    }

    // metrics：与 colors 同一套路，值必须是数字。
    const QJsonObject metrics = json.value(QStringLiteral("metrics")).toObject();
    for (const QString &key : metrics.keys()) {
        if (baseline.isValid() && !baseline.metrics.contains(key)) {
            warnUnknown(fileName, QStringLiteral("metrics.") + key);
            continue;
        }
        const QJsonValue v = metrics.value(key);
        if (v.isDouble()) {
            out.metrics.insert(key, v.toDouble());
        } else {
            qWarning().noquote() << QStringLiteral(
                "ThemeLoader: %1.json metrics.%2 is not a number; using the "
                "baseline value").arg(fileName, key);
            if (baseline.isValid()) {
                out.metrics.insert(key, baseline.metrics.value(key));
            }
        }
    }

    // fonts：只认 family/monoFamily，值必须是字符串；非法值直接忽略
    // （无 baseline 兜底——字体空串本身就是合法的「跟随系统」）。
    const QJsonObject fonts = json.value(QStringLiteral("fonts")).toObject();
    for (const QString &key : fonts.keys()) {
        if (!kFontKeys.contains(key)) {
            warnUnknown(fileName, QStringLiteral("fonts.") + key);
            continue;
        }
        const QJsonValue v = fonts.value(key);
        if (v.isString()) {
            out.fonts.insert(key, v.toString());
        }
        else {
            qWarning().noquote() << QStringLiteral(
                "ThemeLoader: %1.json fonts.%2 must be a string; ignoring it")
                .arg(fileName, key);
        }
    }

    // agentPalette：数组里每项都必须是合法颜色字符串，坏项忽略。
    const QJsonValue palette = json.value(QStringLiteral("agentPalette"));
    if (palette.isArray()) {
        for (const QJsonValue &v : palette.toArray()) {
            if (v.isString() && QColor(v.toString()).isValid()) {
                out.agentPalette.append(v.toString());
            }
            else {
                qWarning().noquote() << QStringLiteral(
                    "ThemeLoader: %1.json agentPalette has a non-color entry; "
                    "ignoring it").arg(fileName);
            }
        }
    } else if (!palette.isUndefined()) {
        qWarning().noquote() << QStringLiteral(
            "ThemeLoader: %1.json agentPalette must be an array; ignoring it")
            .arg(fileName);
    }

    // 缺失令牌从同 variant 的 baseline 补齐，主题只需声明它改的部分。
    if (baseline.isValid()) {
        for (auto it = baseline.colors.constBegin();
             it != baseline.colors.constEnd(); ++it) {
            if (!out.colors.contains(it.key())) {
                out.colors.insert(it.key(), it.value());
            }
        }
        for (auto it = baseline.metrics.constBegin();
             it != baseline.metrics.constEnd(); ++it) {
            if (!out.metrics.contains(it.key())) {
                out.metrics.insert(it.key(), it.value());
            }
        }
        for (auto it = baseline.fonts.constBegin();
             it != baseline.fonts.constEnd(); ++it) {
            if (!out.fonts.contains(it.key())) {
                out.fonts.insert(it.key(), it.value());
            }
        }
        if (out.agentPalette.isEmpty()) {
            out.agentPalette = baseline.agentPalette;
        }
    }

    return true;
}

/**
 * @brief 读一个主题文件并解析
 *
 * 从路径提取裸文件名（最后一段去掉 .json），交给 parse() 按
 * 「文件名 = id」校验。任何失败都返回无效 ThemeFile：调用方按
 * 「没有这个主题」处理，具体原因已在日志里告警。
 *
 * @param path     文件路径（磁盘路径或 :/ 资源路径）
 * @param baseline 同 variant 的基线主题；传无效 ThemeFile 表示加载
 *                 内置主题本身
 * @return 解析成功且通过校验的主题；否则为无效 ThemeFile
 * @sa parse
 */
ThemeFile ThemeLoader::loadFile(const QString &path, const ThemeFile &baseline)
{
    ThemeFile result;
    const QString fileName =
        path.section(QLatin1Char('/'), -1).section(QStringLiteral(".json"), 0, 0);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral(
            "ThemeLoader: cannot read %1: %2").arg(path, file.errorString());
        return result;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning().noquote() << QStringLiteral(
            "ThemeLoader: %1 is not a valid JSON object; skipping it").arg(path);
        return result;
    }
    parse(doc.object(), fileName, baseline, result);
    return result;
}

} // namespace awb::theme
