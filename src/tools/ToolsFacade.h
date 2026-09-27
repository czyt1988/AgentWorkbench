#ifndef AWB_TOOLS_TOOLSFACADE_H
#define AWB_TOOLS_TOOLSFACADE_H

#include "core/OpResult.h"
#include "tools/ToolsStore.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QVariantList>

class QTimer;

namespace awb::tools {

class FileTreeModel;

/// Agent Tools 页的 QML 门面：工作区记忆、当前工作区、提示词草稿与文件树模型。
///
/// 校验与草稿防抖落盘见 ToolsFacade.cpp；持久化是 ToolsStore 的 tools.json。
class ToolsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    /// 工作区绝对路径列表，MRU 在前；ComboBox 直接绑定。
    Q_PROPERTY(QVariantList workspaces READ workspaces NOTIFY workspacesChanged)
    Q_PROPERTY(QString currentWorkspace READ currentWorkspace
               WRITE setCurrentWorkspace NOTIFY currentWorkspaceChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)

public:
    explicit ToolsFacade(const QString &dataRoot, QObject *parent = nullptr);
    ~ToolsFacade() override;

    QAbstractItemModel *model() const;
    FileTreeModel *fileTreeModel() const;

    QVariantList workspaces() const;
    QString currentWorkspace() const;
    QString draft() const;

    /// 校验目录存在后交给 ToolsStore（MRU + 上限淘汰），并把树根切到新工作区。
    /// 失败带可展示的原因（目录不存在 / 落盘失败）。
    Q_INVOKABLE core::OpResult addWorkspace(const QString &path);

    /// 移除一个工作区；移除的是当前项时树根顺延到队首剩余项。
    Q_INVOKABLE core::OpResult removeWorkspace(const QString &path);

    /// 重扫文件树；结果经 refreshFinished 广播（异步形状，当前实现同步完成）。
    Q_INVOKABLE void refresh();

    // Q_PROPERTY WRITE 侧。
    void setCurrentWorkspace(const QString &path);
    void setDraft(const QString &text);

signals:
    void workspacesChanged();
    void currentWorkspaceChanged();
    void draftChanged();
    /// 一次重扫完成（手动 refresh 或 watcher 触发都会走到这里）。
    void refreshFinished();

private:
    /// 把当前工作区设为树根；目录已不存在时不设根（树保持空）。
    void applyCurrentToModel();
    /// 草稿防抖到期/析构时的落盘动作。
    void persistDraft();

    ToolsStore m_store;
    FileTreeModel *m_model;
    QString m_draft;
    QTimer *m_draftTimer;
};

} // namespace awb::tools

#endif // AWB_TOOLS_TOOLSFACADE_H
