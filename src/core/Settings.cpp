#include "core/Settings.h"

#include "core/JsonStore.h"
#include "core/Paths.h"

#include <QJsonObject>
#include <QSet>

namespace awb::core {

namespace {

// Warn about keys that are not part of the schema (§7.2: unknown keys are
// ignored and logged, never fatal).
void warnUnknownKeys(const QJsonObject &obj, const QSet<QString> &known,
                     const QString &where)
{
    for (const QString &key : obj.keys()) {
        if (known.contains(key))
            continue;
        qWarning().noquote() << QStringLiteral(
            "Settings: unknown key %1 in settings.json; ignoring it")
            .arg(where.isEmpty() ? key : where + QLatin1Char('.') + key);
    }
}

QString readString(const QJsonObject &obj, const QString &key,
                   const QString &fallback, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined())
        return fallback;
    if (!v.isString()) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be a string; using the default")
            .arg(where, key);
        return fallback;
    }
    return v.toString();
}

bool readBool(const QJsonObject &obj, const QString &key, bool fallback,
              const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined())
        return fallback;
    if (!v.isBool()) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be a boolean; using the default")
            .arg(where, key);
        return fallback;
    }
    return v.toBool();
}

int readInt(const QJsonObject &obj, const QString &key, int fallback,
            int minValue, int maxValue, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined())
        return fallback;
    if (!v.isDouble() || v.toInt() != v.toDouble()
        || v.toInt() < minValue || v.toInt() > maxValue) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be an integer in [%3, %4]; using the "
            "default").arg(where, key).arg(minValue).arg(maxValue);
        return fallback;
    }
    return v.toInt();
}

qint64 readInt64(const QJsonObject &obj, const QString &key, qint64 fallback,
                 qint64 minValue, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined())
        return fallback;
    if (!v.isDouble() || v.toDouble() < double(minValue)
        || v.toDouble() > 9.007199254740992e15) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be a number >= %3; using the default")
            .arg(where, key).arg(minValue);
        return fallback;
    }
    return qint64(v.toDouble());
}

QStringList readStringList(const QJsonObject &obj, const QString &key,
                           const QStringList &fallback, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined())
        return fallback;
    if (!v.isArray()) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be an array; using the default")
            .arg(where, key);
        return fallback;
    }
    QStringList out;
    for (const QJsonValue &item : v.toArray()) {
        if (!item.isString()) {
            qWarning().noquote() << QStringLiteral(
                "Settings: %1.%2 must contain only strings; ignoring the "
                "entry").arg(where, key);
            continue;
        }
        out.append(item.toString());
    }
    return out;
}

const QSet<QString> kWindowKeys = {
    QStringLiteral("title"), QStringLiteral("width"), QStringLiteral("height"),
    QStringLiteral("sidebarWidth"), QStringLiteral("sidebarCollapsed"),
    QStringLiteral("lastPageId") };
const QSet<QString> kAppearanceKeys = { QStringLiteral("theme"),
                                        QStringLiteral("followSystem") };
const QSet<QString> kLocaleKeys = { QStringLiteral("override") };
const QSet<QString> kLauncherKeys = { QStringLiteral("healthCheckIntervalMs"),
                                      QStringLiteral("startupVersionCheck") };
const QSet<QString> kWebKeys = {
    QStringLiteral("surface"), QStringLiteral("freezeInactiveTabs"),
    QStringLiteral("maxLiveTabs"), QStringLiteral("downloadDir"),
    QStringLiteral("chromiumFlags"), QStringLiteral("homeUrl") };
const QSet<QString> kSkillsKeys = { QStringLiteral("roots"),
                                    QStringLiteral("includePluginCaches"),
                                    QStringLiteral("maxDepth") };
const QSet<QString> kLoggingKeys = { QStringLiteral("maxFileSize"),
                                     QStringLiteral("maxFiles") };
const QSet<QString> kPluginsKeys = { QStringLiteral("enabled"),
                                     QStringLiteral("disabledIds") };
const QSet<QString> kRootKeys = {
    QStringLiteral("window"), QStringLiteral("appearance"),
    QStringLiteral("locale"), QStringLiteral("launcher"),
    QStringLiteral("web"), QStringLiteral("skills"),
    QStringLiteral("logging"), QStringLiteral("plugins") };

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    load();
}

QString Settings::settingsFilePath()
{
    return Paths::dataRoot() + QStringLiteral("/settings.json");
}

QString Settings::themeId() const
{
    return m_appearance.theme;
}

QString Settings::windowTitle() const
{
    return m_window.title;
}

QJsonArray Settings::skillsRoots() const
{
    return m_skills.roots;
}

void Settings::load()
{
    const QJsonObject root = JsonStore::readFile(settingsFilePath());
    warnUnknownKeys(root, kRootKeys, QString());

    const QJsonObject window = root.value(QStringLiteral("window")).toObject();
    warnUnknownKeys(window, kWindowKeys, QStringLiteral("window"));
    m_window.title = readString(window, QStringLiteral("title"),
                                m_window.title, QStringLiteral("window"));
    m_window.width = readInt(window, QStringLiteral("width"), m_window.width,
                             400, 16384, QStringLiteral("window"));
    m_window.height = readInt(window, QStringLiteral("height"), m_window.height,
                              300, 16384, QStringLiteral("window"));
    m_window.sidebarWidth = readInt(window, QStringLiteral("sidebarWidth"),
                                    m_window.sidebarWidth, 0, 1024,
                                    QStringLiteral("window"));
    m_window.sidebarCollapsed =
        readBool(window, QStringLiteral("sidebarCollapsed"),
                 m_window.sidebarCollapsed, QStringLiteral("window"));
    m_window.lastPageId = readString(window, QStringLiteral("lastPageId"),
                                     m_window.lastPageId,
                                     QStringLiteral("window"));

    const QJsonObject appearance =
        root.value(QStringLiteral("appearance")).toObject();
    warnUnknownKeys(appearance, kAppearanceKeys, QStringLiteral("appearance"));
    m_appearance.theme = readString(appearance, QStringLiteral("theme"),
                                    m_appearance.theme,
                                    QStringLiteral("appearance"));
    m_appearance.followSystem =
        readBool(appearance, QStringLiteral("followSystem"),
                 m_appearance.followSystem, QStringLiteral("appearance"));

    const QJsonObject locale = root.value(QStringLiteral("locale")).toObject();
    warnUnknownKeys(locale, kLocaleKeys, QStringLiteral("locale"));
    m_locale.overrideName = readString(locale, QStringLiteral("override"),
                                       m_locale.overrideName,
                                       QStringLiteral("locale"));

    const QJsonObject launcher = root.value(QStringLiteral("launcher")).toObject();
    warnUnknownKeys(launcher, kLauncherKeys, QStringLiteral("launcher"));
    m_launcher.healthCheckIntervalMs =
        readInt(launcher, QStringLiteral("healthCheckIntervalMs"),
                m_launcher.healthCheckIntervalMs, 100, 600000,
                QStringLiteral("launcher"));
    m_launcher.startupVersionCheck =
        readBool(launcher, QStringLiteral("startupVersionCheck"),
                 m_launcher.startupVersionCheck, QStringLiteral("launcher"));

    const QJsonObject web = root.value(QStringLiteral("web")).toObject();
    warnUnknownKeys(web, kWebKeys, QStringLiteral("web"));
    m_web.surface = readString(web, QStringLiteral("surface"), m_web.surface,
                               QStringLiteral("web"));
    if (m_web.surface != QLatin1String("embedded")
        && m_web.surface != QLatin1String("external")) {
        qWarning().noquote() << QStringLiteral(
            "Settings: web.surface must be \"embedded\" or \"external\"; "
            "using the default");
        m_web.surface = WebSettings().surface;
    }
    m_web.freezeInactiveTabs =
        readBool(web, QStringLiteral("freezeInactiveTabs"),
                 m_web.freezeInactiveTabs, QStringLiteral("web"));
    m_web.maxLiveTabs = readInt(web, QStringLiteral("maxLiveTabs"),
                                m_web.maxLiveTabs, 1, 64, QStringLiteral("web"));
    m_web.downloadDir = readString(web, QStringLiteral("downloadDir"),
                                   m_web.downloadDir, QStringLiteral("web"));
    m_web.chromiumFlags = readString(web, QStringLiteral("chromiumFlags"),
                                     m_web.chromiumFlags, QStringLiteral("web"));
    m_web.homeUrl = readString(web, QStringLiteral("homeUrl"), m_web.homeUrl,
                               QStringLiteral("web"));

    const QJsonObject skills = root.value(QStringLiteral("skills")).toObject();
    warnUnknownKeys(skills, kSkillsKeys, QStringLiteral("skills"));
    if (skills.contains(QStringLiteral("roots"))) {
        const QJsonValue v = skills.value(QStringLiteral("roots"));
        if (v.isArray())
            m_skills.roots = v.toArray();
        else
            qWarning().noquote() << QStringLiteral(
                "Settings: skills.roots must be an array; using the default");
    }
    m_skills.includePluginCaches =
        readBool(skills, QStringLiteral("includePluginCaches"),
                 m_skills.includePluginCaches, QStringLiteral("skills"));
    m_skills.maxDepth = readInt(skills, QStringLiteral("maxDepth"),
                                m_skills.maxDepth, 1, 32,
                                QStringLiteral("skills"));

    const QJsonObject logging = root.value(QStringLiteral("logging")).toObject();
    warnUnknownKeys(logging, kLoggingKeys, QStringLiteral("logging"));
    m_logging.maxFileSize = readInt64(logging, QStringLiteral("maxFileSize"),
                                      m_logging.maxFileSize, 1024,
                                      QStringLiteral("logging"));
    m_logging.maxFiles = readInt(logging, QStringLiteral("maxFiles"),
                                 m_logging.maxFiles, 1, 20,
                                 QStringLiteral("logging"));

    const QJsonObject plugins = root.value(QStringLiteral("plugins")).toObject();
    warnUnknownKeys(plugins, kPluginsKeys, QStringLiteral("plugins"));
    m_plugins.enabled = readBool(plugins, QStringLiteral("enabled"),
                                 m_plugins.enabled, QStringLiteral("plugins"));
    m_plugins.disabledIds = readStringList(
        plugins, QStringLiteral("disabledIds"), m_plugins.disabledIds,
        QStringLiteral("plugins"));
}

void Settings::setWindowTitle(const QString &title)
{
    m_window.title = title;
    emit valueChanged(QStringLiteral("window.title"));
}

void Settings::setThemeId(const QString &id)
{
    m_appearance.theme = id;
    emit valueChanged(QStringLiteral("appearance.theme"));
}

void Settings::setFollowSystem(bool on)
{
    m_appearance.followSystem = on;
    emit valueChanged(QStringLiteral("appearance.followSystem"));
}

void Settings::setWindowSize(int width, int height)
{
    m_window.width = width;
    m_window.height = height;
    emit valueChanged(QStringLiteral("window.width"));
    emit valueChanged(QStringLiteral("window.height"));
}

void Settings::setSidebarCollapsed(bool collapsed)
{
    m_window.sidebarCollapsed = collapsed;
    emit valueChanged(QStringLiteral("window.sidebarCollapsed"));
}

void Settings::setLastPageId(const QString &pageId)
{
    m_window.lastPageId = pageId;
    emit valueChanged(QStringLiteral("window.lastPageId"));
}

void Settings::setWebSurface(const QString &surface)
{
    m_web.surface = surface;
    emit valueChanged(QStringLiteral("web.surface"));
}

void Settings::setWebChromiumFlags(const QString &flags)
{
    m_web.chromiumFlags = flags;
    emit valueChanged(QStringLiteral("web.chromiumFlags"));
}

void Settings::setSkillRoots(const QJsonArray &roots)
{
    m_skills.roots = roots;
    emit valueChanged(QStringLiteral("skills.roots"));
}

void Settings::setPluginsDisabledIds(const QStringList &ids)
{
    m_plugins.disabledIds = ids;
    emit valueChanged(QStringLiteral("plugins.disabledIds"));
}

void Settings::setPluginsGloballyEnabled(bool enabled)
{
    m_plugins.enabled = enabled;
    emit valueChanged(QStringLiteral("plugins.enabled"));
}

OpResult Settings::save()
{
    QJsonObject window;
    window[QStringLiteral("title")] = m_window.title;
    window[QStringLiteral("width")] = m_window.width;
    window[QStringLiteral("height")] = m_window.height;
    window[QStringLiteral("sidebarWidth")] = m_window.sidebarWidth;
    window[QStringLiteral("sidebarCollapsed")] = m_window.sidebarCollapsed;
    window[QStringLiteral("lastPageId")] = m_window.lastPageId;

    QJsonObject appearance;
    appearance[QStringLiteral("theme")] = m_appearance.theme;
    appearance[QStringLiteral("followSystem")] = m_appearance.followSystem;

    QJsonObject locale;
    locale[QStringLiteral("override")] = m_locale.overrideName;

    QJsonObject launcher;
    launcher[QStringLiteral("healthCheckIntervalMs")] =
        m_launcher.healthCheckIntervalMs;
    launcher[QStringLiteral("startupVersionCheck")] =
        m_launcher.startupVersionCheck;

    QJsonObject web;
    web[QStringLiteral("surface")] = m_web.surface;
    web[QStringLiteral("freezeInactiveTabs")] = m_web.freezeInactiveTabs;
    web[QStringLiteral("maxLiveTabs")] = m_web.maxLiveTabs;
    web[QStringLiteral("downloadDir")] = m_web.downloadDir;
    web[QStringLiteral("chromiumFlags")] = m_web.chromiumFlags;
    web[QStringLiteral("homeUrl")] = m_web.homeUrl;

    QJsonObject skills;
    skills[QStringLiteral("roots")] = m_skills.roots;
    skills[QStringLiteral("includePluginCaches")] = m_skills.includePluginCaches;
    skills[QStringLiteral("maxDepth")] = m_skills.maxDepth;

    QJsonObject logging;
    logging[QStringLiteral("maxFileSize")] = double(m_logging.maxFileSize);
    logging[QStringLiteral("maxFiles")] = m_logging.maxFiles;

    QJsonObject plugins;
    plugins[QStringLiteral("enabled")] = m_plugins.enabled;
    QJsonArray disabled;
    for (const QString &id : m_plugins.disabledIds)
        disabled.append(id);
    plugins[QStringLiteral("disabledIds")] = disabled;

    QJsonObject root;
    root[QStringLiteral("window")] = window;
    root[QStringLiteral("appearance")] = appearance;
    root[QStringLiteral("locale")] = locale;
    root[QStringLiteral("launcher")] = launcher;
    root[QStringLiteral("web")] = web;
    root[QStringLiteral("skills")] = skills;
    root[QStringLiteral("logging")] = logging;
    root[QStringLiteral("plugins")] = plugins;

    const OpResult result = JsonStore::writeFile(settingsFilePath(), root);
    if (!result.ok)
        qWarning().noquote() << QStringLiteral("Settings: save failed: %1")
                                    .arg(result.error);
    return result;
}

} // namespace awb::core
