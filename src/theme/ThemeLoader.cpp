#include "theme/ThemeLoader.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace awb::theme {

namespace {

// Top-level keys of a theme file.
const QSet<QString> kTopLevelKeys = {
    QStringLiteral("id"),          QStringLiteral("name"),
    QStringLiteral("variant"),     QStringLiteral("author"),
    QStringLiteral("description"), QStringLiteral("colors"),
    QStringLiteral("metrics"),     QStringLiteral("fonts"),
    QStringLiteral("agentPalette"),
};

const QSet<QString> kFontKeys = {QStringLiteral("family"),
                                 QStringLiteral("monoFamily")};

void warnUnknown(const QString &where, const QString &key)
{
    qWarning().noquote() << QStringLiteral(
        "ThemeLoader: unknown key %1 in %2; ignoring it").arg(key, where);
}

} // namespace

bool ThemeLoader::parse(const QJsonObject &json, const QString &fileName,
                        const ThemeFile &baseline, ThemeFile &out)
{
    out = ThemeFile();

    for (const QString &key : json.keys()) {
        if (!kTopLevelKeys.contains(key))
            warnUnknown(fileName, key);
    }

    const QString id = json.value(QStringLiteral("id")).toString();
    const QString name = json.value(QStringLiteral("name")).toString();
    const QString variant = json.value(QStringLiteral("variant")).toString();

    // Required fields: a file that cannot identify itself is skipped whole.
    if (id.isEmpty() || name.isEmpty()
        || (variant != QLatin1String("dark")
            && variant != QLatin1String("light"))) {
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

    // Colors: unknown tokens ignored, invalid colors fall back to the
    // baseline, missing tokens filled from the baseline afterwards.
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
            if (baseline.isValid())
                out.colors.insert(key, baseline.colors.value(key));
        }
    }

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
            if (baseline.isValid())
                out.metrics.insert(key, baseline.metrics.value(key));
        }
    }

    const QJsonObject fonts = json.value(QStringLiteral("fonts")).toObject();
    for (const QString &key : fonts.keys()) {
        if (!kFontKeys.contains(key)) {
            warnUnknown(fileName, QStringLiteral("fonts.") + key);
            continue;
        }
        const QJsonValue v = fonts.value(key);
        if (v.isString())
            out.fonts.insert(key, v.toString());
        else
            qWarning().noquote() << QStringLiteral(
                "ThemeLoader: %1.json fonts.%2 must be a string; ignoring it")
                .arg(fileName, key);
    }

    const QJsonValue palette = json.value(QStringLiteral("agentPalette"));
    if (palette.isArray()) {
        for (const QJsonValue &v : palette.toArray()) {
            if (v.isString() && QColor(v.toString()).isValid())
                out.agentPalette.append(v.toString());
            else
                qWarning().noquote() << QStringLiteral(
                    "ThemeLoader: %1.json agentPalette has a non-color entry; "
                    "ignoring it").arg(fileName);
        }
    } else if (!palette.isUndefined()) {
        qWarning().noquote() << QStringLiteral(
            "ThemeLoader: %1.json agentPalette must be an array; ignoring it")
            .arg(fileName);
    }

    // Fill missing tokens from the baseline of the same variant, so a theme
    // only has to declare what it changes.
    if (baseline.isValid()) {
        for (auto it = baseline.colors.constBegin();
             it != baseline.colors.constEnd(); ++it) {
            if (!out.colors.contains(it.key()))
                out.colors.insert(it.key(), it.value());
        }
        for (auto it = baseline.metrics.constBegin();
             it != baseline.metrics.constEnd(); ++it) {
            if (!out.metrics.contains(it.key()))
                out.metrics.insert(it.key(), it.value());
        }
        for (auto it = baseline.fonts.constBegin();
             it != baseline.fonts.constEnd(); ++it) {
            if (!out.fonts.contains(it.key()))
                out.fonts.insert(it.key(), it.value());
        }
        if (out.agentPalette.isEmpty())
            out.agentPalette = baseline.agentPalette;
    }

    return true;
}

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
