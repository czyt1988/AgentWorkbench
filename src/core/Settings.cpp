#include "core/Settings.h"

#include "core/JsonStore.h"
#include "core/Logging.h"
#include "core/Paths.h"

#include <QDebug>
#include <QJsonObject>
#include <QSet>
#include <utility>

namespace awb::core {

namespace {

/**
 * @brief 对不属于 schema 的键记一条警告
 *
 * 未知键被忽略并记录，从不致命。
 *
 * @param obj   待检查的对象
 * @param known 该对象已知的合法键集合
 * @param where 键的所属段名（如 "window"）；根级传空串
 */
void warnUnknownKeys(const QJsonObject &obj, const QSet<QString> &known,
                     const QString &where)
{
    for (const QString &key : obj.keys()) {
        if (known.contains(key)) {
            continue;
        }
        qWarning().noquote() << QStringLiteral(
            "Settings: unknown key %1 in settings.json; ignoring it")
            .arg(where.isEmpty() ? key : where + QLatin1Char('.') + key);
    }
}

/**
 * @brief 读一个字符串键
 *
 * @param obj      待读对象
 * @param key      键名
 * @param fallback 键缺失时返回的默认值
 * @param where    键的所属段名，用于告警文本
 * @return 键值；缺失或类型不对（记警告）时返回 fallback
 */
QString readString(const QJsonObject &obj, const QString &key,
                   const QString &fallback, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined()) {
        return fallback;
    }
    if (!v.isString()) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be a string; using the default")
            .arg(where, key);
        return fallback;
    }
    return v.toString();
}

/**
 * @brief 读一个布尔键
 *
 * @param obj      待读对象
 * @param key      键名
 * @param fallback 键缺失时返回的默认值
 * @param where    键的所属段名，用于告警文本
 * @return 键值；缺失或类型不对（记警告）时返回 fallback
 */
bool readBool(const QJsonObject &obj, const QString &key, bool fallback,
              const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined()) {
        return fallback;
    }
    if (!v.isBool()) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be a boolean; using the default")
            .arg(where, key);
        return fallback;
    }
    return v.toBool();
}

/**
 * @brief 读一个带上下限的整数键
 *
 * @param obj      待读对象
 * @param key      键名
 * @param fallback 键缺失时返回的默认值
 * @param minValue 合法下界（含）
 * @param maxValue 合法上界（含）
 * @param where    键的所属段名，用于告警文本
 * @return 键值；缺失、非整数或越界（记警告）时返回 fallback
 */
int readInt(const QJsonObject &obj, const QString &key, int fallback,
            int minValue, int maxValue, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined()) {
        return fallback;
    }
    if (!v.isDouble() || v.toInt() != v.toDouble()
        || v.toInt() < minValue || v.toInt() > maxValue) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be an integer in [%3, %4]; using the "
            "default").arg(where, key).arg(minValue).arg(maxValue);
        return fallback;
    }
    return v.toInt();
}

/**
 * @brief 读一个 64 位整数键
 *
 * JSON 数字是双精度，这里校验其在 qint64 可精确表示的范围内
 * （9.007199254740992e15 即 2^53）。
 *
 * @param obj      待读对象
 * @param key      键名
 * @param fallback 键缺失时返回的默认值
 * @param minValue 合法下界（含）
 * @param where    键的所属段名，用于告警文本
 * @return 键值；缺失、非数或越界（记警告）时返回 fallback
 */
qint64 readInt64(const QJsonObject &obj, const QString &key, qint64 fallback,
                 qint64 minValue, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined()) {
        return fallback;
    }
    if (!v.isDouble() || v.toDouble() < double(minValue)
        || v.toDouble() > 9.007199254740992e15) {
        qWarning().noquote() << QStringLiteral(
            "Settings: %1.%2 must be a number >= %3; using the default")
            .arg(where, key).arg(minValue);
        return fallback;
    }
    return qint64(v.toDouble());
}

/**
 * @brief 读一个字符串数组键
 *
 * @param obj      待读对象
 * @param key      键名
 * @param fallback 键缺失时返回的默认值
 * @param where    键的所属段名，用于告警文本
 * @return 键值；缺失或不是数组（记警告）时返回 fallback；
 *         非字符串元素被跳过并记警告
 */
QStringList readStringList(const QJsonObject &obj, const QString &key,
                           const QStringList &fallback, const QString &where)
{
    const QJsonValue v = obj.value(key);
    if (v.isUndefined()) {
        return fallback;
    }
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

/// 各段与根级对象的合法键集合，warnUnknownKeys() 的比对基准。
const QSet<QString> kWindowKeys = {
    QStringLiteral("title"), QStringLiteral("width"), QStringLiteral("height"),
    QStringLiteral("sidebarWidth"), QStringLiteral("sidebarCollapsed"),
    QStringLiteral("lastPageId") };
const QSet<QString> kAppearanceKeys = { QStringLiteral("theme"),
                                        QStringLiteral("followSystem"),
                                        QStringLiteral("fontFamily") };
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
                                     QStringLiteral("maxFiles"),
                                     QStringLiteral("level"),
                                     QStringLiteral("mirrorToStderr") };
const QSet<QString> kPluginsKeys = { QStringLiteral("enabled"),
                                     QStringLiteral("disabledIds") };
const QSet<QString> kRootKeys = {
    QStringLiteral("window"), QStringLiteral("appearance"),
    QStringLiteral("locale"), QStringLiteral("launcher"),
    QStringLiteral("web"), QStringLiteral("skills"),
    QStringLiteral("logging"), QStringLiteral("plugins") };

} // namespace

/**
 * @brief 构造并从 settings.json 载入全部配置
 *
 * @param parent QObject 父项
 */
Settings::Settings(QObject *parent)
    : QObject(parent)
{
    load();
}

/**
 * @brief 取 settings.json 的路径
 *
 * @return <dataRoot>/settings.json
 */
QString Settings::settingsFilePath()
{
    return Paths::dataRoot() + QStringLiteral("/settings.json");
}

/**
 * @brief 取当前主题 id
 *
 * @return appearance.theme
 */
QString Settings::themeId() const
{
    return m_appearance.theme;
}

/**
 * @brief 取 UI 全局字体族
 *
 * @return appearance.fontFamily
 */
QString Settings::fontFamily() const
{
    return m_appearance.fontFamily;
}

/**
 * @brief 取窗口标题
 *
 * @return window.title
 */
QString Settings::windowTitle() const
{
    return m_window.title;
}

/**
 * @brief 取 skill 扫描根
 *
 * @return skills.roots；空数组表示用内置默认根
 */
QJsonArray Settings::skillsRoots() const
{
    return m_skills.roots;
}

/**
 * @brief 从 settings.json 读入全部键
 *
 * 每段先经 warnUnknownKeys() 过滤未知键，再逐键读取：缺键取结构体的
 * 默认值，类型不对或越界记警告后同样取默认值——单个坏键不影响其余键。
 */
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
    m_appearance.fontFamily =
        readString(appearance, QStringLiteral("fontFamily"),
                   m_appearance.fontFamily, QStringLiteral("appearance"));

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
    if (m_web.surface != QStringLiteral("embedded")
        && m_web.surface != QStringLiteral("external")) {
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
        if (v.isArray()) {
            m_skills.roots = v.toArray();
        }
        else {
            qWarning().noquote() << QStringLiteral(
                "Settings: skills.roots must be an array; using the default");
        }
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
    m_logging.level = readString(logging, QStringLiteral("level"),
                                 m_logging.level, QStringLiteral("logging"));
    if (!Logging::isValidLevelName(m_logging.level)) {
        qWarning().noquote() << QStringLiteral(
            "Settings: logging.level \"%1\" is not one of debug, info, "
            "warning, critical, off; using the default")
                                .arg(m_logging.level);
        m_logging.level = LoggingSettings().level;
    }
    m_logging.mirrorToStderr = readBool(
        logging, QStringLiteral("mirrorToStderr"), m_logging.mirrorToStderr,
        QStringLiteral("logging"));

    const QJsonObject plugins = root.value(QStringLiteral("plugins")).toObject();
    warnUnknownKeys(plugins, kPluginsKeys, QStringLiteral("plugins"));
    m_plugins.enabled = readBool(plugins, QStringLiteral("enabled"),
                                 m_plugins.enabled, QStringLiteral("plugins"));
    m_plugins.disabledIds = readStringList(
        plugins, QStringLiteral("disabledIds"), m_plugins.disabledIds,
        QStringLiteral("plugins"));
}

/**
 * @brief 写窗口标题
 *
 * @param title 新标题；空串表示用应用默认
 */
void Settings::setWindowTitle(const QString &title)
{
    m_window.title = title;
    Q_EMIT valueChanged(QStringLiteral("window.title"));
}

/**
 * @brief 写主题 id
 *
 * @param id 新主题 id
 */
void Settings::setThemeId(const QString &id)
{
    m_appearance.theme = id;
    Q_EMIT valueChanged(QStringLiteral("appearance.theme"));
}

/**
 * @brief 写是否跟随系统深浅色
 *
 * @param on true = 跟随系统
 */
void Settings::setFollowSystem(bool on)
{
    m_appearance.followSystem = on;
    Q_EMIT valueChanged(QStringLiteral("appearance.followSystem"));
}

/**
 * @brief 写 UI 全局字体族
 *
 * @param family 新字体族；空串表示跟随主题/系统默认
 */
void Settings::setFontFamily(const QString &family)
{
    m_appearance.fontFamily = family;
    Q_EMIT valueChanged(QStringLiteral("appearance.fontFamily"));
}

/**
 * @brief 写窗口尺寸
 *
 * @param width  新宽度（像素）
 * @param height 新高度（像素）
 */
void Settings::setWindowSize(int width, int height)
{
    m_window.width = width;
    m_window.height = height;
    Q_EMIT valueChanged(QStringLiteral("window.width"));
    Q_EMIT valueChanged(QStringLiteral("window.height"));
}

/**
 * @brief 写侧栏折叠状态
 *
 * @param collapsed true = 折叠
 */
void Settings::setSidebarCollapsed(bool collapsed)
{
    m_window.sidebarCollapsed = collapsed;
    Q_EMIT valueChanged(QStringLiteral("window.sidebarCollapsed"));
}

/**
 * @brief 写上次停留的页面 id
 *
 * @param pageId 页面 id
 */
void Settings::setLastPageId(const QString &pageId)
{
    m_window.lastPageId = pageId;
    Q_EMIT valueChanged(QStringLiteral("window.lastPageId"));
}

/**
 * @brief 写 Web 展示面（embedded | external）
 *
 * @param surface 新的展示面名
 */
void Settings::setWebSurface(const QString &surface)
{
    m_web.surface = surface;
    Q_EMIT valueChanged(QStringLiteral("web.surface"));
}

/**
 * @brief 写 Chromium 命令行开关
 *
 * @param flags 空格分隔的 Chromium flags
 */
void Settings::setWebChromiumFlags(const QString &flags)
{
    m_web.chromiumFlags = flags;
    Q_EMIT valueChanged(QStringLiteral("web.chromiumFlags"));
}

/**
 * @brief 写 skill 扫描根
 *
 * @param roots 根路径数组；空数组表示用内置默认根
 */
void Settings::setSkillRoots(const QJsonArray &roots)
{
    m_skills.roots = roots;
    Q_EMIT valueChanged(QStringLiteral("skills.roots"));
}

/**
 * @brief 写被禁用插件的 id 列表
 *
 * @param ids 插件 id 列表
 */
void Settings::setPluginsDisabledIds(const QStringList &ids)
{
    m_plugins.disabledIds = ids;
    Q_EMIT valueChanged(QStringLiteral("plugins.disabledIds"));
}

/**
 * @brief 写插件总开关
 *
 * @param enabled true = 允许加载插件
 */
void Settings::setPluginsGloballyEnabled(bool enabled)
{
    m_plugins.enabled = enabled;
    Q_EMIT valueChanged(QStringLiteral("plugins.enabled"));
}

/**
 * @brief 把全部设置写回 settings.json
 *
 * 原子写入（经 JsonStore::writeFile），写入失败记一条警告并把失败
 * 结果原样返回给调用方。
 *
 * @return 写入结果
 */
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
    appearance[QStringLiteral("fontFamily")] = m_appearance.fontFamily;

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
    logging[QStringLiteral("level")] = m_logging.level;
    logging[QStringLiteral("mirrorToStderr")] = m_logging.mirrorToStderr;

    QJsonObject plugins;
    plugins[QStringLiteral("enabled")] = m_plugins.enabled;
    QJsonArray disabled;
    for (const QString &id : std::as_const(m_plugins.disabledIds)) {
        disabled.append(id);
    }
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
    if (!result.ok) {
        qWarning().noquote() << QStringLiteral("Settings: save failed: %1")
                                    .arg(result.error);
    }
    return result;
}

} // namespace awb::core
