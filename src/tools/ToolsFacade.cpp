#include "tools/ToolsFacade.h"

#include "tools/FileTreeFlatModel.h"
#include "tools/FileTreeModel.h"

#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QTimer>

namespace awb::tools {

namespace {

/**
 * @brief 草稿落盘的防抖间隔（毫秒）
 *
 * 逐字符写盘没有意义，500 ms 停顿后写一次足够；应用退出由析构兜底补写。
 */
constexpr int kDraftSaveDelayMs = 500;
} // namespace

/**
 * @brief 构造 Agent Tools 页的门面
 *
 * 构造后立即可用：加载 tools.json、恢复草稿、装好防抖与信号转发、
 * 叠加用户图标表（必须在设根之前，见 FileTreeModel::loadUserIconFile）、
 * 把扁平投影接到树源，最后按记忆的当前工作区设根。
 *
 * @param dataRoot 应用数据根
 * @param parent QObject 父项
 */
ToolsFacade::ToolsFacade(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_store(dataRoot)
    , m_model(new FileTreeModel(this))
    , m_flatModel(new FileTreeFlatModel(this))
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

    // QML 的树视图消费扁平投影（Qt 5/Qt 6 同一份 delegate，见
    // FileTreeFlatModel）。
    m_flatModel->setSourceModel(m_model);

    applyCurrentToModel();
}

/**
 * @brief 析构门面
 *
 * 防抖计时器里还压着的最后一段草稿不能丢：计时器活跃时立即补写一次。
 */
ToolsFacade::~ToolsFacade()
{
    if (m_draftTimer->isActive()) {
        persistDraft();
    }
}

/**
 * @brief 取 QML 消费的模型（Q_PROPERTY 的 READ 侧）
 *
 * @return 树的扁平投影；树源本体经 fileTreeModel() 取
 */
QAbstractItemModel *ToolsFacade::model() const
{
    return m_flatModel;
}

/**
 * @brief 取树源本体
 *
 * 扁平投影消费它；直接使用树模型的只有测试与内部。
 *
 * @return 树模型
 */
FileTreeModel *ToolsFacade::fileTreeModel() const
{
    return m_model;
}

/**
 * @brief 取工作区列表（Q_PROPERTY 的 READ 侧）
 *
 * @return 工作区绝对路径列表，MRU 在前
 */
QVariantList ToolsFacade::workspaces() const
{
    QVariantList list;
    for (const QString &path : m_store.workspaces()) {
        list.append(path);
    }
    return list;
}

/**
 * @brief 取当前工作区（Q_PROPERTY 的 READ 侧）
 *
 * @return 绝对路径；无当前工作区时为空串
 */
QString ToolsFacade::currentWorkspace() const
{
    return m_store.currentWorkspace();
}

/**
 * @brief 取草稿全文（Q_PROPERTY 的 READ 侧）
 *
 * @return 草稿文本；从未写过时为空串
 */
QString ToolsFacade::draft() const
{
    return m_draft;
}

/**
 * @brief 新增工作区并切换到它
 *
 * 返回类型必须写全限定名 awb::core::OpResult：Qt 5 的 moc 按头文件书写
 * 形式记录返回类型名，而 QML 调用端按 QMetaType 注册名（即类全名）
 * 解析；短名解析不到注册表就抛 "Unknown method return type"，按钮
 * 静默无响应。
 *
 * @param path 用户选的目录路径
 * @return 失败时 ok = false 且 error 是可直接展示的 tr() 源串
 *         （目录不存在 / 落盘失败）
 */
core::OpResult ToolsFacade::addWorkspace(const QString &path)
{
    if (!QFileInfo(path).isDir()) {
        return core::OpResult::failure(tr("The selected path is not a folder."));
    }
    if (!m_store.addWorkspace(path)) {
        // 列表与树已更新但 tools.json 没写进去；下次启动记忆丢失，其余功能不受影响。
        qWarning() << "[tools] could not persist tools.json";
        return core::OpResult::failure(tr("Could not save the workspace list."));
    }
    applyCurrentToModel();
    Q_EMIT workspacesChanged();
    Q_EMIT currentWorkspaceChanged();
    return core::OpResult::success();
}

/**
 * @brief 移除一个工作区
 *
 * 移除的是当前项时，树根经 applyCurrentToModel() 顺延到队首剩余项。
 * 落盘失败只记警告并仍按成功返回——列表与树已更新，丢的只是记忆。
 *
 * @param path 要移除的工作区路径
 * @return 不在列表中时失败；否则成功
 */
core::OpResult ToolsFacade::removeWorkspace(const QString &path)
{
    if (!m_store.workspaces().contains(path)) {
        return core::OpResult::failure(tr("The workspace is not in the list."));
    }
    if (!m_store.removeWorkspace(path)) {
        qWarning() << "[tools] could not persist tools.json";
    }
    applyCurrentToModel();
    Q_EMIT workspacesChanged();
    Q_EMIT currentWorkspaceChanged();
    return core::OpResult::success();
}

/**
 * @brief 重扫文件树（异步形状，当前实现同步完成）
 *
 * 立即返回；结果经 refreshFinished 广播。watcher 触发的重扫在模型内部
 * 完成、经同一信号转发，QML 无需区分来源。
 */
void ToolsFacade::refresh()
{
    m_model->refresh();
}

/**
 * @brief 生成插入编辑区的文件引用文本
 *
 * 拖拽/双击文件树条目时 QML 调用：拼成 `./相对路径`（反引号包裹、
 * 正斜杠分隔）。
 *
 * @param relativePath 相对工作区根的路径（正斜杠，无 ./ 前缀）
 * @return 引用文本；空路径返回空串
 */
QString ToolsFacade::fileReference(const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return QString();
    }
    return QStringLiteral("`./") + relativePath + QStringLiteral("`");
}

/**
 * @brief 切换当前工作区（Q_PROPERTY 的 WRITE 侧）
 *
 * 空串 = 清空当前工作区（树随之清空）；列表外路径不接受（QML 只会传
 * 列表项）。切换后该项移到 MRU 队首。
 *
 * @param path 列表内的工作区路径或空串
 */
void ToolsFacade::setCurrentWorkspace(const QString &path)
{
    if (!path.isEmpty() && !m_store.workspaces().contains(path)) {
        return;
    }
    if (path == m_store.currentWorkspace()) {
        return;
    }
    m_store.setCurrentWorkspace(path);
    applyCurrentToModel();
    Q_EMIT workspacesChanged();
    Q_EMIT currentWorkspaceChanged();
}

/**
 * @brief 覆盖草稿（Q_PROPERTY 的 WRITE 侧）
 *
 * 只更新缓冲并重启防抖计时器，落盘由 persistDraft() 在停顿后执行。
 *
 * @param text 草稿全文
 */
void ToolsFacade::setDraft(const QString &text)
{
    if (text == m_draft) {
        return;
    }
    m_draft = text;
    Q_EMIT draftChanged();
    m_draftTimer->start();
}

/**
 * @brief 把记忆里的当前工作区设为树根
 *
 * 目录已不存在时不设根：树保持空白，用户从下拉里换走或移除这一项，
 * 比让树显示一个永远为空的假根直观。
 */
void ToolsFacade::applyCurrentToModel()
{
    const QString current = m_store.currentWorkspace();
    m_model->setRootPath(QDir(current).exists() ? current : QString());
}

/**
 * @brief 把草稿缓冲写进 tools.json
 *
 * 防抖到期或析构时调用；失败只记警告（下一段草稿或下次启动会再试）。
 */
void ToolsFacade::persistDraft()
{
    if (!m_store.setDraft(m_draft)) {
        qWarning() << "[tools] could not persist the draft";
    }
}

} // namespace awb::tools
