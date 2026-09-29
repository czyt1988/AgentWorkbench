#include "theme/ThemeRegistry.h"

#include "core/Paths.h"
#include "theme/ThemeLoader.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <utility>

namespace awb::theme {

namespace {

/// 内置主题 id，同时是 themes() 的固定输出顺序（mocha-dark 在前——
/// 它是未知 id 的回退主题，latte-light 在后）。
const QStringList kBuiltinIds = {QStringLiteral("mocha-dark"),
                                 QStringLiteral("latte-light")};

/**
 * @brief 读一个 JSON 文件并取其顶层对象
 *
 * @param path 文件路径（磁盘或 :/ 资源）
 * @return 顶层 JSON 对象；打不开或内容不是 JSON 对象时返回空对象
 *         （已告警，调用方按「无内容」跳过）
 */
QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral(
            "ThemeRegistry: cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning().noquote() << QStringLiteral(
            "ThemeRegistry: %1 is not a valid JSON object; skipping it")
            .arg(path);
        return {};
    }
    return doc.object();
}

} // namespace

/**
 * @brief 构造主题注册表
 *
 * 先装内置主题（构造上完整，用无效 baseline 解析——没有兜底，也没有
 * 顶层键清单之外的未知键过滤），再创建用户主题目录（热重载要监视它，
 * 目录不存在监视器挂不上；空目录 = 还没有用户主题），最后扫描用户主题
 * 并挂监视。
 *
 * @param parent QObject 父项
 */
ThemeRegistry::ThemeRegistry(QObject *parent)
    : QObject(parent)
{
    // 内置在前：它们构造上完整，用无效 baseline 解析——没有兜底，也没有
    // 顶层键清单之外的未知键过滤。
    for (const QString &id : kBuiltinIds) {
        const QString path = QStringLiteral(":/themes/") + id
                             + QStringLiteral(".json");
        ThemeFile file = ThemeLoader::loadFile(path, ThemeFile());
        if (file.isValid()) {
            m_builtins.insert(id, file);
            m_themes.insert(id, file);
            m_sources.insert(id, path);
        }
    }

    // 用户主题目录要被监视（热重载），所以提前建出来；空 = 还没有用户主题。
    QDir().mkpath(core::Paths::themesDir());

    scan();
    armWatchers();

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this]() { refresh(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) { refresh(); });
}

/**
 * @brief 取全部主题的稳定顺序清单
 *
 * 内置主题按 kBuiltinIds 的固定顺序在前，其余用户主题按 id 不区分大小
 * 写排序在后。选择界面（Theme::availableThemes）依赖该顺序做「内置在
 * 前」的展示，不能随 QHash 的任意序漂移。
 *
 * @return 全部生效主题（用户覆盖已应用）
 */
QList<ThemeFile> ThemeRegistry::themes() const
{
    // 内置按固定顺序在前，用户独有主题随后。
    QList<ThemeFile> result;
    QSet<QString> seen;
    for (const QString &id : kBuiltinIds) {
        if (m_themes.contains(id)) {
            result.append(m_themes.value(id));
            seen.insert(id);
        }
    }
    QStringList extra = m_themes.keys();
    extra.removeAll(QStringLiteral("mocha-dark"));
    extra.removeAll(QStringLiteral("latte-light"));
    extra.sort(Qt::CaseInsensitive);
    for (const QString &id : std::as_const(extra)) {
        if (!seen.contains(id)) {
            result.append(m_themes.value(id));
        }
    }
    return result;
}

/**
 * @brief 按 id 取生效主题
 *
 * @param id 主题 id
 * @return 对应主题（用户覆盖已应用）；未知 id 返回无效 ThemeFile
 */
ThemeFile ThemeRegistry::theme(const QString &id) const
{
    return m_themes.value(id);
}

/**
 * @brief 取某 variant 的内置基线主题
 *
 * 用户主题解析时的兜底来源（ThemeLoader 用它补缺失令牌），所以永远取
 * 内置版本，不受同 id 用户覆盖影响。
 *
 * @param variant "dark" 或 "light"
 * @return 对应的内置 ThemeFile；未知 variant 返回无效 ThemeFile
 * @sa ThemeLoader::parse
 */
ThemeFile ThemeRegistry::baseline(const QString &variant) const
{
    if (variant == QStringLiteral("dark")) {
        return m_builtins.value(QStringLiteral("mocha-dark"));
    }
    if (variant == QStringLiteral("light")) {
        return m_builtins.value(QStringLiteral("latte-light"));
    }
    return {};
}

/**
 * @brief 重扫全部主题并通知变化
 *
 * 监视器的两条信号（directoryChanged / fileChanged）都汇到这里：重扫、
 * 重挂监视（文件可能新增或消失），最后发 changed() 让 Theme 热重载。
 */
void ThemeRegistry::refresh()
{
    scan();
    armWatchers();
    Q_EMIT changed();
}

/**
 * @brief 重建生效主题表
 *
 * 从内置表起步，逐个解析用户目录里的 *.json（目录按名排序）：readJson
 * 失败或被 ThemeLoader::parse 判定跳过的文件直接忽略，其余按 id 覆盖/
 * 追加进 m_themes 并记录来源路径供挂监视。
 */
void ThemeRegistry::scan()
{
    m_themes = m_builtins;
    m_sources.clear();
    for (const QString &id : kBuiltinIds) {
        m_sources.insert(id, QStringLiteral(":/themes/") + id
                                 + QStringLiteral(".json"));
    }

    const QString userDir = core::Paths::themesDir();
    const QDir dir(userDir);
    const QStringList files = dir.entryList({QStringLiteral("*.json")},
                                            QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        const QString path = dir.filePath(fileName);
        const QJsonObject json = readJson(path);
        if (json.isEmpty()) {
            continue;
        }
        const QString variant =
            json.value(QStringLiteral("variant")).toString();
        ThemeFile file;
        if (!ThemeLoader::parse(json, fileName.section(QLatin1Char('.'), 0, 0),
                                baseline(variant), file)) {
            continue;
        }
        // 同 id 的用户文件覆盖内置版本。
        m_themes.insert(file.id, file);
        m_sources.insert(file.id, path);
    }
}

/**
 * @brief 重挂文件监视
 *
 * 从头重挂：重扫后文件可能新增、被替换或消失，旧清单不再成立。只监视
 * 用户目录与用户文件——内置主题编译进 qrc，进程内不会变。
 */
void ThemeRegistry::armWatchers()
{
    // 从头重挂：重扫后文件可能新增、被替换或消失。
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }

    const QString userDir = core::Paths::themesDir();
    m_watcher.addPath(userDir);

    QSet<QString> userFiles;
    for (auto it = m_sources.constBegin(); it != m_sources.constEnd(); ++it) {
        if (it.value().startsWith(userDir)) {
            userFiles.insert(it.value());
        }
    }
    if (!userFiles.isEmpty()) {
        m_watcher.addPaths(userFiles.values());
    }
}

} // namespace awb::theme
