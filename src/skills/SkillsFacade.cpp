#include "skills/SkillsFacade.h"

#include "core/Settings.h"
#include "skills/SkillModel.h"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QUrl>

namespace awb::skills {

SkillsFacade::SkillsFacade(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(new SkillModel(this))
    , m_scanner(new SkillScanner(settings, this))
{
    connect(m_scanner, &SkillScanner::scanStarted, this, [this]() {
        m_scanning = true;
        emit scanningChanged();
        emit scanStarted();
    });
    connect(m_scanner, &SkillScanner::scanFinished, this, [this]() {
        m_model->setSkills(m_scanner->definitions());
        m_scanning = false;
        emit scanningChanged();
        emit statsChanged();
        emit scanFinished();
    });
}

QAbstractItemModel *SkillsFacade::model() const
{
    return m_model;
}

QString SkillsFacade::statsText() const
{
    const SkillScanner::Stats stats = m_scanner->lastStats();
    // One line: count + scan time + skipped roots (footer).
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

bool SkillsFacade::partialFailure() const
{
    return m_scanner->lastStats().rootsSkipped > 0;
}

void SkillsFacade::refresh()
{
    m_scanner->refresh();
}

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

void SkillsFacade::setRootEnabled(const QString &id, bool enabled)
{
    m_scanner->setRootEnabled(id, enabled);
    emit rootsChanged();
    emit statsChanged();
}

bool SkillsFacade::addRoot(const QString &path)
{
    if (path.trimmed().isEmpty())
        return false;
    // Serialize the effective list plus the new entry (a non-empty
    // skills.roots fully replaces the defaults.
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
    for (const SkillRoot &root : m_scanner->roots())
        write(root);

    SkillRoot custom;
    custom.path = path.trimmed();
    custom.label = QFileInfo(custom.path).fileName();
    custom.kind = QStringLiteral("custom");
    custom.id = QStringLiteral("custom-%1").arg(array.size());
    write(custom);

    m_settings->setSkillRoots(array);
    const core::OpResult saved = m_settings->save();
    if (!saved.ok)
        qWarning().noquote() << QStringLiteral(
            "SkillsFacade: could not persist skill roots: %1").arg(saved.error);
    refresh();
    emit rootsChanged();
    return true;
}

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
    if (!removed)
        return false;
    m_settings->setSkillRoots(array);
    const core::OpResult saved = m_settings->save();
    if (!saved.ok)
        qWarning().noquote() << QStringLiteral(
            "SkillsFacade: could not persist skill roots: %1").arg(saved.error);
    refresh();
    emit rootsChanged();
    return true;
}

QString SkillsFacade::parentDir(const QString &skillFilePath)
{
    return QFileInfo(skillFilePath).absolutePath();
}

core::OpResult SkillsFacade::copyToClipboard(const QString &text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return core::OpResult::failure(tr("The clipboard is not available."));
    clipboard->setText(text);
    return core::OpResult::success();
}

core::OpResult SkillsFacade::copyPath(const QString &skillFilePath)
{
    const QString dir = parentDir(skillFilePath);
    if (dir.isEmpty())
        return core::OpResult::failure(tr("Unknown skill."));
    return copyToClipboard(dir);
}

core::OpResult SkillsFacade::copySkillFile(const QString &skillFilePath)
{
    if (!QFile::exists(skillFilePath))
        return core::OpResult::failure(tr("Unknown skill."));
    return copyToClipboard(skillFilePath);
}

core::OpResult SkillsFacade::copyName(const QString &skillFilePath)
{
    const SkillDefinition *found = find(skillFilePath);
    if (!found)
        return core::OpResult::failure(tr("Unknown skill."));
    return copyToClipboard(found->name);
}

core::OpResult SkillsFacade::openFolder(const QString &skillFilePath)
{
    const QString dir = parentDir(skillFilePath);
    const QFileInfo info(dir);
    if (!info.exists() || !info.isDir())
        return core::OpResult::failure(tr("Not a directory: %1").arg(dir));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath())))
        return core::OpResult::failure(
            tr("Could not open the folder: %1").arg(dir));
    return core::OpResult::success();
}

core::OpResult SkillsFacade::revealSkillFile(const QString &skillFilePath)
{
    const QFileInfo info(skillFilePath);
    if (!info.exists())
        return core::OpResult::failure(
            tr("The path does not exist: %1").arg(skillFilePath));
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
    qint64 pid = 0;
    if (!QProcess::startDetached(QStringLiteral("explorer"),
                                 {QStringLiteral("/select,") + native},
                                 QString(), &pid))
        return core::OpResult::failure(tr("Could not open the file manager."));
    return core::OpResult::success();
#else
    return openFolder(skillFilePath);
#endif
}

QString SkillsFacade::kindLabel(const QString &kind) const
{
    // One shared mapping (the Settings and Skills pages both bind facets
    // and root rows through it); unknown kinds pass through unchanged.
    if (kind == QLatin1String("agents"))
        return tr("Agents");
    if (kind == QLatin1String("claude"))
        return tr("Claude");
    if (kind == QLatin1String("codex"))
        return tr("Codex");
    if (kind == QLatin1String("plugin"))
        return tr("Plugin");
    if (kind == QLatin1String("project"))
        return tr("Project");
    if (kind == QLatin1String("custom"))
        return tr("Custom");
    return kind;
}

QVariantMap SkillsFacade::skill(const QString &skillFilePath) const
{
    QVariantMap map;
    const SkillDefinition *found = find(skillFilePath);
    if (!found)
        return map;
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

const SkillDefinition *SkillsFacade::find(const QString &skillFilePath) const
{
    const QList<SkillDefinition> &all = m_scanner->definitions();
    for (const SkillDefinition &skill : all) {
        if (skill.skillFilePath == skillFilePath)
            return &skill;
    }
    return nullptr;
}

} // namespace awb::skills
