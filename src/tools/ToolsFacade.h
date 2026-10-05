#ifndef AWB_TOOLS_TOOLSFACADE_H
#define AWB_TOOLS_TOOLSFACADE_H

#include "core/OpResult.h"
#include "tools/PromptChecks.h"
#include "tools/PromptTemplateLibrary.h"
#include "tools/ToolsStore.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QTimer;

namespace awb::tools {

class FileTreeFlatModel;
class FileTreeModel;

/// Agent Tools 页的 QML 门面：工作区记忆、当前工作区、提示词草稿与文件
/// 树模型，外加模板库与收藏夹（M2）。
///
/// 校验与草稿防抖落盘见 ToolsFacade.cpp；持久化是 ToolsStore 的 tools.json
/// 与 PromptTemplateLibrary 的用户模板目录。
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
    /// 活跃草稿正文（M4：多草稿中**当前**那条的镜像；旧 draft 键的语义）
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
    /// 草稿清单（创建序）：[{id,title,workspace,updatedAt}]（不含正文——
    /// 正文按 id 经 draftBody() 取或经 setDraftBody 写）
    Q_PROPERTY(QVariantList drafts READ drafts NOTIFY draftsChanged)
    /// 当前草稿 id；空 = 无草稿（界面给新建引导）
    Q_PROPERTY(QString activeDraftId READ activeDraftId
               WRITE setActiveDraftId NOTIFY activeDraftIdChanged)
    /// 模板清单（合并视图）：[{id,title,category,tags,description,builtin}]
    Q_PROPERTY(QVariantList templates READ templates NOTIFY templatesChanged)
    /// 收藏夹（新在前）：[{title,body,tags,createdAt}]
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged)
    /// Ribbon 是否折叠（工作台窗口的界面偏好，写盘经 ToolsStore）
    Q_PROPERTY(bool ribbonCollapsed READ ribbonCollapsed WRITE setRibbonCollapsed
               NOTIFY ribbonSettingsChanged)
    /// Ribbon 样式档位："single" / "two-row" / "three-row"
    Q_PROPERTY(QString ribbonStyle READ ribbonStyle WRITE setRibbonStyle
               NOTIFY ribbonSettingsChanged)
    /// Ribbon 主题是否跟随应用主题
    Q_PROPERTY(bool ribbonFollowAppTheme READ ribbonFollowAppTheme
               WRITE setRibbonFollowAppTheme NOTIFY ribbonSettingsChanged)
    /// Ribbon 上次停留的标签页索引（从 0 起）
    Q_PROPERTY(int ribbonLastTabIndex READ ribbonLastTabIndex
               WRITE setRibbonLastTabIndex NOTIFY ribbonSettingsChanged)

    // --- 编辑器偏好与体检开关（M3；写盘经 ToolsStore 的 editor/checks 段）
    /// 查找是否区分大小写（B2）
    Q_PROPERTY(bool findCaseSensitive READ findCaseSensitive
               WRITE setFindCaseSensitive NOTIFY editorSettingsChanged)
    /// 查找是否全词匹配（B2）
    Q_PROPERTY(bool findWholeWords READ findWholeWords
               WRITE setFindWholeWords NOTIFY editorSettingsChanged)
    /// 上下文窗口估算基数（C4；>= 1000）
    Q_PROPERTY(int contextWindowTokens READ contextWindowTokens
               WRITE setContextWindowTokens NOTIFY editorSettingsChanged)
    /// 已关闭的体检规则 id 列表（B9；缺键 = 全开）
    Q_PROPERTY(QStringList disabledCheckIds READ disabledCheckIds
               WRITE setDisabledCheckIds NOTIFY checksConfigChanged)
    /// 某条体检规则是否开启（disabledCheckIds 的便捷读侧）
    Q_INVOKABLE bool checkEnabled(const QString &ruleId) const;

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

    // Ribbon 界面偏好（Q_PROPERTY 的 READ 侧；写盘立即，见 ToolsStore）
    bool ribbonCollapsed() const;
    QString ribbonStyle() const;
    bool ribbonFollowAppTheme() const;
    int ribbonLastTabIndex() const;

    // 编辑器偏好（Q_PROPERTY 的 READ 侧）
    bool findCaseSensitive() const;
    bool findWholeWords() const;
    int contextWindowTokens() const;
    // 体检规则开关（Q_PROPERTY 的 READ 侧）
    QStringList disabledCheckIds() const;

    // --- 体检与预算的 QML 入口（M3：纯层经门面供 QML 消费） -----------
    /// 跑体检（已按 disabledCheckIds 过滤）：[{ruleId, severity, start,
    /// length, message}]。空文本得到空列表。
    Q_INVOKABLE QVariantList runChecks(const QString &text) const;
    /// 一条规则的显示名（英文源串；清单标题用）。
    Q_INVOKABLE QString checkRuleTitle(const QString &ruleId) const;
    /// 全部规则 id（稳定顺序——清单顺序用）。
    Q_INVOKABLE QStringList checkRuleIds() const;
    /// 估算 token 数（草稿与文件 tab 的状态条共用，E6 同源）。
    Q_INVOKABLE int estimateTokens(const QString &text) const;
    /// 预算分级：0 安全 / 1 提醒 / 2 超限（窗口基数取 contextWindowTokens）。
    Q_INVOKABLE int budgetLevel(int tokens) const;

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

    // --- 模板与收藏（M2） -----------------------------------------------
    // 模板清单（Q_PROPERTY 的 READ 侧；合并视图见 PromptTemplateLibrary）
    QVariantList templates() const;
    // 检索模板：关键词匹配标题/描述/标签/正文，tag 非空时要求带该标签
    // （与关系）。空关键词 + 空 tag = 全部。
    Q_INVOKABLE QVariantList filteredTemplates(const QString &search,
                                               const QString &tag) const;
    // 模板正文里的待填变量（排除内置变量——它们插入时自动求值）：
    // [{name, defaultValue}]，按出现顺序。
    Q_INVOKABLE QVariantList templateVariables(const QString &id) const;
    // 套值渲染一份模板：values 是表单给的变量值，selection 是插入瞬间
    // 编辑区的选区（{{selection}} 用，空串 = 无）。内置变量在内部求值
    // （workspace = 当前工作区，date = 今天）。未填的变量原样保留
    // `{{name}}` 待办。未知 id 返回空串。
    Q_INVOKABLE QString renderTemplate(const QString &id,
                                       const QVariantMap &values,
                                       const QString &selection) const;
    // 把一段文本存为用户模板（Save as Template）。id 由标题派生（撞名
    // 自动加序号），文件落在用户模板目录，存完列表立即可见。
    Q_INVOKABLE awb::core::OpResult saveAsTemplate(const QString &title,
                                                   const QString &body,
                                                   const QString &category,
                                                   const QString &description);
    // 删除一份用户模板（内置的删不掉——随包再生）
    Q_INVOKABLE awb::core::OpResult removeTemplate(const QString &id);
    // 用户模板目录（「打开模板目录」动作用）
    Q_INVOKABLE QString templateDirectory() const;

    // 收藏夹（Q_PROPERTY 的 READ 侧）
    QVariantList favorites() const;
    // 收藏一段成稿（同标题同全文 = 幂等重存，不占新位）
    Q_INVOKABLE void addFavorite(const QString &title, const QString &body);
    // 按标题 + 全文移除一条收藏
    Q_INVOKABLE void removeFavorite(const QString &title,
                                    const QString &body);

    // --- 草稿集（M4） ------------------------------------------------------
    // 草稿清单（Q_PROPERTY 的 READ 侧；创建序）
    QVariantList drafts() const;
    // 当前草稿 id（Q_PROPERTY 的 READ 侧）
    QString activeDraftId() const;
    // 某条草稿的正文（渲染镜像/编辑器初值的取用入口）
    Q_INVOKABLE QString draftBody(const QString &id) const;
    // 新建草稿并设为当前：标题空时给 "Untitled N"，工作区默认绑当前
    // 工作区。返回 {ok, id, error}；上限拒绝与写盘失败都带原因。
    Q_INVOKABLE awb::core::OpResult createDraft(const QString &title,
                                                const QString &body,
                                                const QString &workspace);
    // 重命名（改名不改 id——tab 恢复按 id 不受影响）
    Q_INVOKABLE awb::core::OpResult renameDraft(const QString &id,
                                                const QString &title);
    // 复制一条草稿（新 id、标题加 " copy"、同工作区；不切激活）
    Q_INVOKABLE awb::core::OpResult duplicateDraft(const QString &id);
    // 删除草稿（连带快照与恢复副本记录）。删的是当前草稿时激活位顺延。
    Q_INVOKABLE awb::core::OpResult removeDraft(const QString &id);
    // 切换当前草稿（id 必须存在）。旧草稿有改动先落盘。
    Q_INVOKABLE awb::core::OpResult switchDraft(const QString &id);
    // 活跃草稿的正文回写（编辑防抖的落点）
    Q_INVOKABLE void setDraftBody(const QString &id, const QString &body);

    // --- 历史快照（M4） ------------------------------------------------------
    // 某条草稿的快照（新在前）：[{body, reason, createdAt, charCount}]
    Q_INVOKABLE QVariantList snapshots(const QString &id) const;
    // 存一版（reason: copy/clear/replace/switch/manual/rollback）
    Q_INVOKABLE void addSnapshot(const QString &id, const QString &body,
                                 const QString &reason);
    // 回滚：确认过破坏性的旧内容替换——先给当前版存一版（rollback 原因），
    // 再把正文换成快照内容。返回 {ok, body}（body = 回滚后的正文）。
    Q_INVOKABLE awb::core::OpResult rollbackToSnapshot(const QString &id,
                                                       qint64 createdAt);

    // --- 崩溃恢复（M4，D6） ---------------------------------------------------
    // 启动时问一次：有没有恢复副本（返回 null 或 {draftId, body}）
    Q_INVOKABLE QVariantMap pendingRecovery() const;
    // 恢复 = 把副本内容变成一条新草稿（标题标注来源），删除副本。
    Q_INVOKABLE QString recoverToNewDraft();
    // 丢弃副本（用户选了不恢复）
    Q_INVOKABLE void discardRecovery();
    // 编辑期降频落恢复副本（窗口的恢复定时器调；2 s 停顿节流）
    Q_INVOKABLE void writeRecoverySnapshot(const QString &id,
                                            const QString &body);

    // Q_PROPERTY WRITE 侧
    void setCurrentWorkspace(const QString &path);
    void setDraft(const QString &text);
    void setActiveDraftId(const QString &id);
    void setRibbonCollapsed(bool collapsed);
    void setRibbonStyle(const QString &style);
    void setRibbonFollowAppTheme(bool follow);
    void setRibbonLastTabIndex(int index);
    void setFindCaseSensitive(bool caseSensitive);
    void setFindWholeWords(bool wholeWords);
    void setContextWindowTokens(int tokens);
    void setDisabledCheckIds(const QStringList &ids);

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
     * @brief 草稿清单或其中某项变化时发射（新建、删除、重命名、复制）
     */
    void draftsChanged();
    /**
     * @brief 当前草稿变化时发射（切换、新建、删除引起的顺延）
     */
    void activeDraftIdChanged();
    /**
     * @brief 任一 Ribbon 界面偏好变化时发射（四个值共用一条信号）
     */
    void ribbonSettingsChanged();
    /**
     * @brief 一次重扫完成（手动 refresh 或 watcher 触发都会走到这里）
     */
    void refreshFinished();
    /**
     * @brief 模板清单变化时发射（存为模板、删除、目录被外部改动后 refresh）
     */
    void templatesChanged();
    /**
     * @brief 收藏夹变化时发射（新增、移除）
     */
    void favoritesChanged();
    /**
     * @brief 任一编辑器偏好变化时发射（三个值共用一条信号）
     */
    void editorSettingsChanged();
    /**
     * @brief 体检规则开关集变化时发射
     */
    void checksConfigChanged();

private:
    // 把当前工作区设为树根；目录已不存在时不设根（树保持空）
    void applyCurrentToModel();
    // 草稿防抖到期/析构时的落盘动作
    void persistDraft();
    // 落盘一份新的 Ribbon 偏好并广播 ribbonSettingsChanged()
    void applyRibbon(const RibbonSettings &ribbon);

    ToolsStore m_store;              ///< tools.json 的工作区记忆与草稿
    PromptTemplateLibrary m_templates;  ///< 模板库（内置 + 用户目录）
    FileTreeModel *m_model;          ///< 树本体（懒加载、watcher、增量对账）
    FileTreeFlatModel *m_flatModel;  ///< QML 消费的扁平投影
    QString m_draft;                 ///< 活跃草稿全文（防抖写盘的缓冲）
    QString m_activeDraftIdCache;    ///< 活跃草稿 id 的门面侧缓存（信号判断用）
    QTimer *m_draftTimer;            ///< 草稿防抖定时器（单次、500 ms）
};

} // namespace awb::tools

#endif // AWB_TOOLS_TOOLSFACADE_H
