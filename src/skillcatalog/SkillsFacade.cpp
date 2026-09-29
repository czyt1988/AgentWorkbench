#include "skillcatalog/SkillsFacade.h"

#include "core/Logging.h"
#include "core/Settings.h"
#include "skillcatalog/SkillCache.h"
#include "skillcatalog/SkillModel.h"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QUrl>

namespace awb::skillcatalog {

/**
 * @brief 构造 skills 门面
 *
 * 组装模型与扫描器，并把 scanner 的信号接到本对象的同名信号上；
 * scanFinished 在转发前先把新定义灌进模型。
 *
 * @param settings 设置访问层（存活期须覆盖本对象）
 * @param parent QObject 父项
 */
SkillsFacade::SkillsFacade(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(new SkillModel(this))
    , m_scanner(new SkillScanner(settings, this))
{
    // 状态转发：scanningChanged 由 scanner 在真实边沿上发射（缓存恢复
    // 不发——页面不会因无变化的信号重放骨架屏动画）。
    connect(m_scanner, &SkillScanner::scanningChanged, this,
            &SkillsFacade::scanningChanged);
    connect(m_scanner, &SkillScanner::scanStarted, this,
            &SkillsFacade::scanStarted);
    connect(m_scanner, &SkillScanner::scanFinished, this, [this]() {
        QElapsedTimer timer;
        timer.start();
        m_model->setSkills(m_scanner->definitions());
        AWB_PERF << QStringLiteral(
            "skills: model reset took %1 ms for %2 skill(s)")
            .arg(timer.elapsed()).arg(m_scanner->definitions().size());
        Q_EMIT statsChanged();
        Q_EMIT scanFinished();
    });
}

/**
 * @brief 取暴露给 QML 的模型
 *
 * @return 模型指针；模型由本门面持有，生命周期随本对象
 */
QAbstractItemModel *SkillsFacade::model() const
{
    return m_model;
}

/**
 * @brief 查询扫描是否进行中
 *
 * @return scanner 的 scanning 状态
 */
bool SkillsFacade::scanning() const
{
    return m_scanner->scanning();
}

/**
 * @brief 启动入口：恢复缓存后发起首扫
 *
 * 同步恢复 JSON 缓存，再发起后台真扫描，详见头文件 start() 的说明。
 */
void SkillsFacade::start()
{
    QElapsedTimer timer;
    timer.start();

    // 缓存恢复是毫秒级的小文件读 + 模型重建，同步做在 GUI 线程上：
    // 这正是「点开 Skills 页立即有内容」的来源，异步化反而会让首屏
    // 闪一次空态。没有缓存（首次启动）时 snapshot 无效，模型保持空，
    // 页面显示扫描提示。
    const SkillCache::Snapshot snapshot = SkillCache::load();
    if (snapshot.isValid()) {
        m_scanner->adoptResults(snapshot.definitions, snapshot.stats);
        AWB_PERF << QStringLiteral(
            "skills: cache restore took %1 ms for %2 skill(s) (cached at %3)")
            .arg(timer.elapsed()).arg(snapshot.definitions.size())
            .arg(snapshot.cachedAt.toString(Qt::ISODate));
    } else {
        AWB_PERF << QStringLiteral(
            "skills: no cache (first start or stale), %1 ms")
            .arg(timer.elapsed());
    }

    // 后台真扫描：结果落地后更新界面并固化缓存（worker 里完成写盘）。
    refresh();
}

/**
 * @brief 取页脚统计文本
 *
 * 一行：总数 + 扫描耗时 +（有跳过时）被跳过的根。
 *
 * @return 已翻译的统计串；还没有任何扫描数据时只有计数部分
 */
QString SkillsFacade::statsText() const
{
    const SkillScanner::Stats stats = m_scanner->lastStats();
    // 一行文本：数量 + 耗时 + 跳过的根（页脚用）。
    QString text = tr("%n skill(s) found", "", m_model->totalCount());
    if (stats.elapsedMs > 0 || stats.skillCount > 0) {
        text += QStringLiteral(" · ")
                + tr("%1 ms").arg(stats.elapsedMs);
    }
    if (stats.rootsSkipped > 0) {
        text += QStringLiteral(" · ")
                + tr("%1 root(s) skipped: %2")
                      .arg(stats.rootsSkipped)
                      .arg(stats.skippedRoots.join(QStringLiteral(", ")));
    }
    return text;
}

/**
 * @brief 查询最近一次扫描是否有根被跳过
 *
 * @return rootsSkipped > 0 时返回 true
 */
bool SkillsFacade::partialFailure() const
{
    return m_scanner->lastStats().rootsSkipped > 0;
}

/**
 * @brief 发起一次后台重新扫描
 *
 * 纯转发到 scanner；进度经 scanStarted/scanFinished/scanningChanged 报告。
 */
void SkillsFacade::refresh()
{
    m_scanner->refresh();
}

/**
 * @brief 取设置页用的根清单
 *
 * @return [{id,label,path,kind,enabled}]；路径保持 RAW 形式
 */
QVariantList SkillsFacade::roots() const
{
    QVariantList list;
    for (const SkillRoot &root : m_scanner->roots()) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = root.id;
        entry[QStringLiteral("label")] = root.label;
        entry[QStringLiteral("path")] = root.path;
        entry[QStringLiteral("kind")] = root.kind;
        entry[QStringLiteral("enabled")] = root.enabled;
        list.append(entry);
    }
    return list;
}

/**
 * @brief 切换一个根的启用状态
 *
 * @param id 根的稳定标识
 * @param enabled 新的启用状态
 */
void SkillsFacade::setRootEnabled(const QString &id, bool enabled)
{
    m_scanner->setRootEnabled(id, enabled);
    Q_EMIT rootsChanged();
    Q_EMIT statsChanged();
}

/**
 * @brief 追加一个自定义根并持久化
 *
 * 新根的 id 按 custom-<序号> 生成，label 取路径最后一段。保存失败
 * 只记日志（清单在内存里已生效）。
 *
 * @param path 根路径（RAW 形式，来自设置页输入）
 * @return 成功追加返回 true；空路径返回 false
 */
bool SkillsFacade::addRoot(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return false;
    }
    // 序列化的是完整生效清单加上新条目（skills.roots 一旦非空
    // 就完全取代默认清单）。
    QJsonArray array;
    const auto write = [&array](const SkillRoot &root) {
        QJsonObject o;
        o[QStringLiteral("id")] = root.id;
        o[QStringLiteral("label")] = root.label;
        o[QStringLiteral("path")] = root.path;
        o[QStringLiteral("kind")] = root.kind;
        o[QStringLiteral("enabled")] = root.enabled;
        array.append(o);
    };
    for (const SkillRoot &root : m_scanner->roots()) {
        write(root);
    }

    SkillRoot custom;
    custom.path = path.trimmed();
    custom.label = QFileInfo(custom.path).fileName();
    custom.kind = QStringLiteral("custom");
    custom.id = QStringLiteral("custom-%1").arg(array.size());
    write(custom);

    m_settings->setSkillRoots(array);
    const core::OpResult saved = m_settings->save();
    if (!saved.ok) {
        qWarning().noquote() << QStringLiteral(
            "SkillsFacade: could not persist skill roots: %1").arg(saved.error);
    }
    refresh();
    Q_EMIT rootsChanged();
    return true;
}

/**
 * @brief 按 id 移除一个根并持久化
 *
 * 保存失败只记日志（清单在内存里已生效）。
 *
 * @param id 根的稳定标识
 * @return 移除成功返回 true；清单里没有该 id 时返回 false 且不落盘
 */
bool SkillsFacade::removeRoot(const QString &id)
{
    QJsonArray array;
    bool removed = false;
    for (const SkillRoot &root : m_scanner->roots()) {
        if (root.id == id) {
            removed = true;
            continue;
        }
        QJsonObject o;
        o[QStringLiteral("id")] = root.id;
        o[QStringLiteral("label")] = root.label;
        o[QStringLiteral("path")] = root.path;
        o[QStringLiteral("kind")] = root.kind;
        o[QStringLiteral("enabled")] = root.enabled;
        array.append(o);
    }
    if (!removed) {
        return false;
    }
    m_settings->setSkillRoots(array);
    const core::OpResult saved = m_settings->save();
    if (!saved.ok) {
        qWarning().noquote() << QStringLiteral(
            "SkillsFacade: could not persist skill roots: %1").arg(saved.error);
    }
    refresh();
    Q_EMIT rootsChanged();
    return true;
}

/**
 * @brief 取 SKILL.md 所在目录
 *
 * @param skillFilePath SKILL.md 的绝对路径
 * @return 其所在目录；路径无效时空串
 */
QString SkillsFacade::parentDir(const QString &skillFilePath)
{
    return QFileInfo(skillFilePath).absolutePath();
}

/**
 * @brief 把文本写进系统剪贴板
 *
 * @param text 要写入的文本
 * @return 写入结果；剪贴板不可用时带可读原因
 */
core::OpResult SkillsFacade::copyToClipboard(const QString &text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return core::OpResult::failure(tr("The clipboard is not available."));
    }
    clipboard->setText(text);
    return core::OpResult::success();
}

/**
 * @brief 复制 skill 的目录路径到剪贴板
 *
 * @param skillFilePath SKILL.md 的路径
 * @return 复制结果；取不到目录时失败
 */
core::OpResult SkillsFacade::copyPath(const QString &skillFilePath)
{
    const QString dir = parentDir(skillFilePath);
    if (dir.isEmpty()) {
        return core::OpResult::failure(tr("Unknown skill."));
    }
    return copyToClipboard(dir);
}

/**
 * @brief 复制 SKILL.md 的完整路径到剪贴板
 *
 * @param skillFilePath SKILL.md 的路径
 * @return 复制结果；文件不存在时失败
 */
core::OpResult SkillsFacade::copySkillFile(const QString &skillFilePath)
{
    if (!QFile::exists(skillFilePath)) {
        return core::OpResult::failure(tr("Unknown skill."));
    }
    return copyToClipboard(skillFilePath);
}

/**
 * @brief 复制 skill 的显示名到剪贴板
 *
 * @param skillFilePath SKILL.md 的路径
 * @return 复制结果；路径不在最近一次结果里时失败
 */
core::OpResult SkillsFacade::copyName(const QString &skillFilePath)
{
    const SkillDefinition *found = find(skillFilePath);
    if (!found) {
        return core::OpResult::failure(tr("Unknown skill."));
    }
    return copyToClipboard(found->name);
}

/**
 * @brief 用系统默认方式打开 skill 所在目录
 *
 * @param skillFilePath SKILL.md 的路径
 * @return 打开结果；目录不存在时失败
 */
core::OpResult SkillsFacade::openFolder(const QString &skillFilePath)
{
    const QString dir = parentDir(skillFilePath);
    const QFileInfo info(dir);
    if (!info.exists() || !info.isDir()) {
        return core::OpResult::failure(tr("Not a directory: %1").arg(dir));
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
        return core::OpResult::failure(
            tr("Could not open the folder: %1").arg(dir));
    }
    return core::OpResult::success();
}

/**
 * @brief 在文件管理器里定位并选中 SKILL.md
 *
 * Windows 上经 explorer /select 实现；其它平台退化为打开所在目录。
 *
 * @param skillFilePath SKILL.md 的路径
 * @return 定位结果；路径不存在或管理器启动失败时失败
 */
core::OpResult SkillsFacade::revealSkillFile(const QString &skillFilePath)
{
    const QFileInfo info(skillFilePath);
    if (!info.exists()) {
        return core::OpResult::failure(
            tr("The path does not exist: %1").arg(skillFilePath));
    }
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
    qint64 pid = 0;
    if (!QProcess::startDetached(QStringLiteral("explorer"),
                                 {QStringLiteral("/select,") + native},
                                 QString(), &pid)) {
        return core::OpResult::failure(tr("Could not open the file manager."));
    }
    return core::OpResult::success();
#else
    return openFolder(skillFilePath);
#endif
}

/**
 * @brief 取根类别 id 的显示名
 *
 * @param kind 类别 id（agents/claude/codex/plugin/project/custom）
 * @return 已翻译的显示名；未知类别原样返回
 */
QString SkillsFacade::kindLabel(const QString &kind) const
{
    // 共用的一张映射（Settings 与 Skills 页面的 facet 和根列表都绑它）；
    // 未知类别原样透传。
    if (kind == QStringLiteral("agents")) {
        return tr("Agents");
    }
    if (kind == QStringLiteral("claude")) {
        return tr("Claude");
    }
    if (kind == QStringLiteral("codex")) {
        return tr("Codex");
    }
    if (kind == QStringLiteral("plugin")) {
        return tr("Plugin");
    }
    if (kind == QStringLiteral("project")) {
        return tr("Project");
    }
    if (kind == QStringLiteral("custom")) {
        return tr("Custom");
    }
    return kind;
}

/**
 * @brief 按 SKILL.md 路径取定义
 *
 * @param skillFilePath SKILL.md 的路径
 * @return 定义字段的 map（悬停 flyout 用）；未知路径返回空 map
 */
QVariantMap SkillsFacade::skill(const QString &skillFilePath) const
{
    QVariantMap map;
    const SkillDefinition *found = find(skillFilePath);
    if (!found) {
        return map;
    }
    map[QStringLiteral("name")] = found->name;
    map[QStringLiteral("description")] = found->description;
    map[QStringLiteral("dirPath")] = found->dirPath;
    map[QStringLiteral("skillFilePath")] = found->skillFilePath;
    map[QStringLiteral("rootLabel")] = found->rootLabel;
    map[QStringLiteral("kind")] = found->kind;
    map[QStringLiteral("pluginId")] = found->pluginId;
    map[QStringLiteral("pluginVersion")] = found->pluginVersion;
    map[QStringLiteral("lastModified")] = found->lastModified;
    map[QStringLiteral("sizeBytes")] = found->sizeBytes;
    map[QStringLiteral("extras")] = found->extras;
    return map;
}

/**
 * @brief 在最近一次扫描结果里按路径查定义
 *
 * @param skillFilePath SKILL.md 的路径
 * @return 匹配的定义指针（归 scanner 的结果列表所有）；找不到返回 nullptr
 */
const SkillDefinition *SkillsFacade::find(const QString &skillFilePath) const
{
    const QList<SkillDefinition> &all = m_scanner->definitions();
    for (const SkillDefinition &skill : all) {
        if (skill.skillFilePath == skillFilePath) {
            return &skill;
        }
    }
    return nullptr;
}

} // namespace awb::skillcatalog
