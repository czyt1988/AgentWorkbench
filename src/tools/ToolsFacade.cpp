#include "tools/ToolsFacade.h"

#include "tools/FileTreeModel.h"

#include <QFileInfo>
#include <QDir>
#include <QTimer>

namespace awb::tools {

/// 草稿落盘防抖：逐字符写盘没有意义，500 ms 停顿后写一次足够；
/// 应用退出由析构兜底补写。
namespace {
constexpr int kDraftSaveDelayMs = 500;
} // namespace

ToolsFacade::ToolsFacade(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_store(dataRoot)
    , m_model(new FileTreeModel(this))
    , m_draftTimer(new QTimer(this))
{
    m_store.load();
    m_draft = m_store.draft();

    m_draftTimer->setSingleShot(true);
    m_draftTimer->setInterval(kDraftSaveDelayMs);
    connect(m_draftTimer, &QTimer::timeout, this, [this]() { persistDraft(); });

    // watcher 触发的重扫在模型内部完成，经此转发给 QML，统一走 refreshFinished。
    connect(m_model, &FileTreeModel::refreshed, this, &ToolsFacade::refreshFinished);

    // 图标表要在设根之前叠加好：模型不会为已经渲染的行补发 dataChanged。
    m_model->loadUserIconFile(dataRoot + QStringLiteral("/file_icons.json"));

    applyCurrentToModel();
}

ToolsFacade::~ToolsFacade()
{
    // 析构兜底：防抖计时器里的最后一段草稿不能丢。
    if (m_draftTimer->isActive())
        persistDraft();
}

QAbstractItemModel *ToolsFacade::model() const
{
    return m_model;
}

FileTreeModel *ToolsFacade::fileTreeModel() const
{
    return m_model;
}

QVariantList ToolsFacade::workspaces() const
{
    QVariantList list;
    for (const QString &path : m_store.workspaces())
        list.append(path);
    return list;
}

QString ToolsFacade::currentWorkspace() const
{
    return m_store.currentWorkspace();
}

QString ToolsFacade::draft() const
{
    return m_draft;
}

core::OpResult ToolsFacade::addWorkspace(const QString &path)
{
    if (!QFileInfo(path).isDir())
        return core::OpResult::failure(tr("The selected path is not a folder."));
    if (!m_store.addWorkspace(path)) {
        // 列表与树已更新但 tools.json 没写进去；下次启动记忆丢失，其余功能不受影响。
        qWarning() << "[tools] could not persist tools.json";
        return core::OpResult::failure(tr("Could not save the workspace list."));
    }
    applyCurrentToModel();
    emit workspacesChanged();
    emit currentWorkspaceChanged();
    return core::OpResult::success();
}

core::OpResult ToolsFacade::removeWorkspace(const QString &path)
{
    if (!m_store.workspaces().contains(path))
        return core::OpResult::failure(tr("The workspace is not in the list."));
    if (!m_store.removeWorkspace(path))
        qWarning() << "[tools] could not persist tools.json";
    applyCurrentToModel();
    emit workspacesChanged();
    emit currentWorkspaceChanged();
    return core::OpResult::success();
}

void ToolsFacade::refresh()
{
    m_model->refresh();
}

QString ToolsFacade::fileReference(const QString &relativePath) const
{
    if (relativePath.isEmpty())
        return QString();
    return QStringLiteral("`./") + relativePath + QStringLiteral("`");
}

void ToolsFacade::setCurrentWorkspace(const QString &path)
{
    // 空串 = 清空当前工作区（树随之清空）；列表外路径不接受（QML 只会传列表项）。
    if (!path.isEmpty() && !m_store.workspaces().contains(path))
        return;
    if (path == m_store.currentWorkspace())
        return;
    m_store.setCurrentWorkspace(path);
    applyCurrentToModel();
    emit workspacesChanged();
    emit currentWorkspaceChanged();
}

void ToolsFacade::setDraft(const QString &text)
{
    if (text == m_draft)
        return;
    m_draft = text;
    emit draftChanged();
    m_draftTimer->start();
}

void ToolsFacade::applyCurrentToModel()
{
    const QString current = m_store.currentWorkspace();
    // 目录没了就不设根：树保持空白，用户从下拉里换走或移除这一项，
    // 比让树显示一个永远为空的假根直观。
    m_model->setRootPath(QDir(current).exists() ? current : QString());
}

void ToolsFacade::persistDraft()
{
    if (!m_store.setDraft(m_draft))
        qWarning() << "[tools] could not persist the draft";
}

} // namespace awb::tools
