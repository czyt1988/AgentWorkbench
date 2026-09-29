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

class FileTreeFlatModel;
class FileTreeModel;

/// Agent Tools 页的 QML 门面：工作区记忆、当前工作区、提示词草稿与文件树模型。
///
/// 校验与草稿防抖落盘见 ToolsFacade.cpp；持久化是 ToolsStore 的 tools.json。
class ToolsFacade : public QObject
{
    Q_OBJECT

    /// 树的扁平投影（FileTreeFlatModel）：QML 的 ListView 消费它，Qt 5/Qt 6
    /// 共用同一份 delegate。树源经 fileTreeModel() 暴露给测试与内部。
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    /// 工作区绝对路径列表，MRU 在前；ComboBox 直接绑定。
    Q_PROPERTY(QVariantList workspaces READ workspaces NOTIFY workspacesChanged)
    Q_PROPERTY(QString currentWorkspace READ currentWorkspace
               WRITE setCurrentWorkspace NOTIFY currentWorkspaceChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)

public:
    // 构造后立即可用：加载 tools.json、建好树模型并把当前工作区设为树根
    explicit ToolsFacade(const QString &dataRoot, QObject *parent = nullptr);
    ~ToolsFacade() override;

    // 树的扁平投影，QML 的 ListView 消费它（Q_PROPERTY 的 READ 侧）
    QAbstractItemModel *model() const;
    // 树源（本体）；测试与内部使用
    FileTreeModel *fileTreeModel() const;

    // 工作区绝对路径列表，MRU 在前
    QVariantList workspaces() const;
    // 当前工作区的绝对路径；无当前工作区时为空串
    QString currentWorkspace() const;
    // 提示词草稿全文
    QString draft() const;

    // 新增工作区：校验目录存在后交给 ToolsStore（MRU + 上限淘汰），并把树根
    // 切到新工作区；失败带可展示的原因。返回类型写全限定名的原因见 .cpp
    Q_INVOKABLE awb::core::OpResult addWorkspace(const QString &path);

    // 移除一个工作区；移除的是当前项时树根顺延到队首剩余项
    Q_INVOKABLE awb::core::OpResult removeWorkspace(const QString &path);

    // 重扫文件树；结果经 refreshFinished 广播（异步形状，当前实现同步完成）
    Q_INVOKABLE void refresh();

    // 生成拖拽/双击插入编辑区的文件引用文本：`./相对路径`（反引号包裹，
    // 正斜杠分隔）；空路径返回空串
    Q_INVOKABLE QString fileReference(const QString &relativePath) const;

    // Q_PROPERTY WRITE 侧
    void setCurrentWorkspace(const QString &path);
    void setDraft(const QString &text);

Q_SIGNALS:
    /**
     * @brief 工作区列表变化时发射（新增、移除、MRU 换序）
     */
    void workspacesChanged();
    /**
     * @brief 当前工作区变化时发射（切换、新增、移除引起的顺延）
     */
    void currentWorkspaceChanged();
    /**
     * @brief 草稿内容变化时发射
     */
    void draftChanged();
    /**
     * @brief 一次重扫完成（手动 refresh 或 watcher 触发都会走到这里）
     */
    void refreshFinished();

private:
    // 把当前工作区设为树根；目录已不存在时不设根（树保持空）
    void applyCurrentToModel();
    // 草稿防抖到期/析构时的落盘动作
    void persistDraft();

    ToolsStore m_store;              ///< tools.json 的工作区记忆与草稿
    FileTreeModel *m_model;          ///< 树本体（懒加载、watcher、增量对账）
    FileTreeFlatModel *m_flatModel;  ///< QML 消费的扁平投影
    QString m_draft;                 ///< 草稿全文（防抖写盘的缓冲）
    QTimer *m_draftTimer;            ///< 草稿防抖定时器（单次、500 ms）
};

} // namespace awb::tools

#endif // AWB_TOOLS_TOOLSFACADE_H
