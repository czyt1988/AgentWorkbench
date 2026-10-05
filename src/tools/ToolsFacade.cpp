#include "tools/ToolsFacade.h"

#include "tools/FileTreeFlatModel.h"
#include "tools/FileTreeModel.h"

#include <QCoreApplication>
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

/**
 * @brief 模板清单 → QML 的 QVariantList
 */
QVariantList templateListToVariant(const QList<PromptTemplate> &templates)
{
    QVariantList result;
    for (const PromptTemplate &template_ : templates) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), template_.id);
        entry.insert(QStringLiteral("title"), template_.title);
        entry.insert(QStringLiteral("category"), template_.category);
        entry.insert(QStringLiteral("tags"), template_.tags);
        entry.insert(QStringLiteral("description"), template_.description);
        entry.insert(QStringLiteral("builtin"),
                     template_.scope == PromptTemplate::Scope::Builtin);
        result.append(entry);
    }
    return result;
}

/**
 * @brief 标题 → 文件名安全的 slug（小写、非字母数字折叠成单个连字符）
 */
QString sanitizeTemplateId(const QString &title)
{
    QString slug;
    bool lastWasDash = true;   // 开头的连字符不要
    for (const QChar &ch : title) {
        const bool wordChar = ch.isLetterOrNumber();
        if (wordChar) {
            slug.append(ch.toLower());
            lastWasDash = false;
        }
        else if (!lastWasDash) {
            slug.append(QLatin1Char('-'));
            lastWasDash = true;
        }
    }
    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }
    return slug.isEmpty() ? QStringLiteral("template") : slug;
}

/**
 * @brief 给 slug 找一个不撞名的 id（fix-a-bug → fix-a-bug-2 → -3 …）
 */
QString uniqueTemplateId(const PromptTemplateLibrary &library,
                         const QString &base)
{
    QString candidate = base;
    int suffix = 2;
    while (!library.byId(candidate).id.isEmpty()) {
        candidate = base + QLatin1Char('-') + QString::number(suffix);
        ++suffix;
    }
    return candidate;
}

/**
 * @brief 正文首行当标题用（去掉 markdown 记号、截到一句以内）
 */
QString firstLineAsTitle(const QString &body)
{
    const QString firstLine =
        body.section(QLatin1Char('\n'), 0, 0).trimmed();
    // 去掉行首的 markdown 记号（#、-、*、>）与行内强调（*、`）。
    QString cleaned;
    for (const QChar &ch : firstLine) {
        if (ch == QLatin1Char('#') || ch == QLatin1Char('-')
            || ch == QLatin1Char('*') || ch == QLatin1Char('>')
            || ch == QLatin1Char('`')) {
            continue;
        }
        cleaned.append(ch);
    }
    cleaned = cleaned.trimmed();
    if (cleaned.size() > 60) {
        cleaned = cleaned.left(60).trimmed() + QStringLiteral("…");
    }
    return cleaned;
}

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
    , m_templates(dataRoot)
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
 * @brief Ribbon 界面偏好的读侧与写侧
 *
 * 四个值都直读直写 ToolsStore（立即落盘、无防抖——开关与标签页切换
 * 本就是低频动作）；写侧先改内存再落盘，落盘失败只记警告，界面状态
 * 以内存为准。四个写侧共用 ribbonSettingsChanged()，QML 侧的绑定一次
 * 重算即可。
 */
///@{
bool ToolsFacade::ribbonCollapsed() const
{
    return m_store.ribbon().collapsed;
}

QString ToolsFacade::ribbonStyle() const
{
    return m_store.ribbon().style;
}

bool ToolsFacade::ribbonFollowAppTheme() const
{
    return m_store.ribbon().followAppTheme;
}

int ToolsFacade::ribbonLastTabIndex() const
{
    return m_store.ribbon().lastTabIndex;
}

void ToolsFacade::setRibbonCollapsed(bool collapsed)
{
    if (m_store.ribbon().collapsed == collapsed) {
        return;
    }
    RibbonSettings ribbon = m_store.ribbon();
    ribbon.collapsed = collapsed;
    applyRibbon(ribbon);
}

void ToolsFacade::setRibbonStyle(const QString &style)
{
    if (m_store.ribbon().style == style) {
        return;
    }
    RibbonSettings ribbon = m_store.ribbon();
    ribbon.style = style;
    applyRibbon(ribbon);
}

void ToolsFacade::setRibbonFollowAppTheme(bool follow)
{
    if (m_store.ribbon().followAppTheme == follow) {
        return;
    }
    RibbonSettings ribbon = m_store.ribbon();
    ribbon.followAppTheme = follow;
    applyRibbon(ribbon);
}

void ToolsFacade::setRibbonLastTabIndex(int index)
{
    const int clamped = index > 0 ? index : 0;
    if (m_store.ribbon().lastTabIndex == clamped) {
        return;
    }
    RibbonSettings ribbon = m_store.ribbon();
    ribbon.lastTabIndex = clamped;
    applyRibbon(ribbon);
}
///@}

/**
 * @brief 把一份新的 Ribbon 偏好写进 store 并广播
 *
 * @param ribbon 新偏好；落盘由 ToolsStore 立即执行
 */
void ToolsFacade::applyRibbon(const RibbonSettings &ribbon)
{
    if (!m_store.setRibbon(ribbon)) {
        qWarning() << "[tools] could not persist the ribbon settings";
    }
    Q_EMIT ribbonSettingsChanged();
}

/**
 * @brief 编辑器偏好与体检开关的读侧/写侧（M3）
 *
 * 与 Ribbon 偏好同一套纪律：直读直写 ToolsStore（立即落盘、低频动作），
 * 落盘失败只记警告、界面状态以内存为准。三个编辑器值共用
 * editorSettingsChanged()，体检开关单独一条 checksConfigChanged()
 * （规则清单是独立的 UI 段）。
 */
///@{
bool ToolsFacade::findCaseSensitive() const
{
    return m_store.editor().findCaseSensitive;
}

bool ToolsFacade::findWholeWords() const
{
    return m_store.editor().findWholeWords;
}

int ToolsFacade::contextWindowTokens() const
{
    return m_store.editor().contextWindowTokens;
}

QStringList ToolsFacade::disabledCheckIds() const
{
    return m_store.disabledCheckIds();
}

bool ToolsFacade::checkEnabled(const QString &ruleId) const
{
    return !m_store.disabledCheckIds().contains(ruleId);
}

void ToolsFacade::setFindCaseSensitive(bool caseSensitive)
{
    if (m_store.editor().findCaseSensitive == caseSensitive) {
        return;
    }
    EditorSettings editor = m_store.editor();
    editor.findCaseSensitive = caseSensitive;
    if (!m_store.setEditor(editor)) {
        qWarning() << "[tools] could not persist the editor settings";
    }
    Q_EMIT editorSettingsChanged();
}

void ToolsFacade::setFindWholeWords(bool wholeWords)
{
    if (m_store.editor().findWholeWords == wholeWords) {
        return;
    }
    EditorSettings editor = m_store.editor();
    editor.findWholeWords = wholeWords;
    if (!m_store.setEditor(editor)) {
        qWarning() << "[tools] could not persist the editor settings";
    }
    Q_EMIT editorSettingsChanged();
}

void ToolsFacade::setContextWindowTokens(int tokens)
{
    // 与 store 读侧同一夹法：0 会把预算条变成「永远超限」。
    const int clamped = qBound(1000, tokens, 1000000);
    if (m_store.editor().contextWindowTokens == clamped) {
        return;
    }
    EditorSettings editor = m_store.editor();
    editor.contextWindowTokens = clamped;
    if (!m_store.setEditor(editor)) {
        qWarning() << "[tools] could not persist the editor settings";
    }
    Q_EMIT editorSettingsChanged();
}

void ToolsFacade::setDisabledCheckIds(const QStringList &ids)
{
    if (m_store.disabledCheckIds() == ids) {
        return;
    }
    if (!m_store.setDisabledCheckIds(ids)) {
        qWarning() << "[tools] could not persist the checks config";
    }
    Q_EMIT checksConfigChanged();
}
///@}

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

// --- 模板与收藏（M2） ----------------------------------------------------

/**
 * @brief 模板清单（合并视图 → QML 的 QVariantList）
 */
QVariantList ToolsFacade::templates() const
{
    return templateListToVariant(m_templates.templates());
}

/**
 * @brief 检索模板（见 PromptTemplateLibrary::filtered 的匹配规则）
 */
QVariantList ToolsFacade::filteredTemplates(const QString &search,
                                           const QString &tag) const
{
    return templateListToVariant(m_templates.filtered(search, tag));
}

/**
 * @brief 模板里的待填变量（排除内置变量；内置的在 renderTemplate 里
 *        自动求值，不进表单）
 */
QVariantList ToolsFacade::templateVariables(const QString &id) const
{
    QVariantList result;
    const PromptTemplate template_ = m_templates.byId(id);
    if (template_.id.isEmpty()) {
        return result;
    }
    const QList<PromptVariables::Variable> variables =
        PromptVariables::extract(template_.body);
    for (const PromptVariables::Variable &variable : variables) {
        if (PromptVariables::isBuiltIn(variable.name)) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), variable.name);
        entry.insert(QStringLiteral("defaultValue"), variable.defaultValue);
        result.append(entry);
    }
    return result;
}

/**
 * @brief 套值渲染一份模板
 *
 * 内置变量在此求值：workspace = 当前工作区、date = 今天、selection =
 * 调用方带入的编辑区选区（它是界面事实，门面自己看不到）。未填的普通
 * 变量原样保留 `{{name}}` 待办——跳过本身是一个有意的动作，不是失败。
 */
QString ToolsFacade::renderTemplate(const QString &id,
                                    const QVariantMap &values,
                                    const QString &selection) const
{
    const PromptTemplate template_ = m_templates.byId(id);
    if (template_.id.isEmpty()) {
        return QString();
    }
    QVariantMap effective = values;
    effective.insert(QStringLiteral("workspace"),
                     m_store.currentWorkspace());
    effective.insert(QStringLiteral("date"),
                     PromptVariables::builtInValue(
                         QStringLiteral("date"), QString()));
    effective.insert(QStringLiteral("selection"), selection);
    return PromptVariables::apply(template_.body, effective);
}

/**
 * @brief 把一段文本存为用户模板
 *
 * id 由标题派生成文件名安全的 slug；撞名自动加序号（不覆盖——「覆盖」
 * 是用户在模板目录里手改同 id 文件的显式动作）。
 */
core::OpResult ToolsFacade::saveAsTemplate(const QString &title,
                                          const QString &body,
                                          const QString &category,
                                          const QString &description)
{
    if (title.trimmed().isEmpty() || body.trimmed().isEmpty()) {
        return core::OpResult::failure(tr(
            "A template needs a title and some text."));
    }
    PromptTemplate template_;
    template_.id = uniqueTemplateId(m_templates,
                                    sanitizeTemplateId(title));
    template_.title = title.trimmed();
    template_.category = category.trimmed().isEmpty()
            ? tr("My templates") : category.trimmed();
    template_.description = description.trimmed();
    template_.body = body.endsWith(QLatin1Char('\n')) ? body
                                                      : body + QLatin1Char('\n');
    template_.scope = PromptTemplate::Scope::User;
    const core::OpResult saved = m_templates.save(template_);
    if (saved) {
        Q_EMIT templatesChanged();
    }
    return saved;
}

/**
 * @brief 删除一份用户模板（内置的随包再生，删不掉）
 */
core::OpResult ToolsFacade::removeTemplate(const QString &id)
{
    const core::OpResult removed = m_templates.remove(id);
    if (removed) {
        Q_EMIT templatesChanged();
    }
    return removed;
}

/**
 * @brief 用户模板目录
 */
QString ToolsFacade::templateDirectory() const
{
    return m_templates.userDirectory();
}

/**
 * @brief 收藏夹（→ QML 的 QVariantList）
 */
QVariantList ToolsFacade::favorites() const
{
    QVariantList result;
    const QList<FavoriteEntry> favorites = m_store.favorites();
    for (const FavoriteEntry &entry : favorites) {
        QVariantMap item;
        item.insert(QStringLiteral("title"), entry.title);
        item.insert(QStringLiteral("body"), entry.body);
        item.insert(QStringLiteral("tags"), entry.tags);
        item.insert(QStringLiteral("createdAt"),
                    double(entry.createdAt));
        result.append(item);
    }
    return result;
}

/**
 * @brief 收藏一段成稿
 */
void ToolsFacade::addFavorite(const QString &title, const QString &body)
{
    const QString trimmed = title.trimmed().isEmpty()
            ? firstLineAsTitle(body) : title.trimmed();
    if (!m_store.addFavorite(trimmed, body, {})) {
        qWarning() << "[tools] could not persist the favorites";
    }
    Q_EMIT favoritesChanged();
}

/**
 * @brief 移除一条收藏
 */
void ToolsFacade::removeFavorite(const QString &title, const QString &body)
{
    if (!m_store.removeFavorite(title, body)) {
        qWarning() << "[tools] could not persist the favorites";
    }
    Q_EMIT favoritesChanged();
}

// --- 体检与预算的 QML 入口（M3） ------------------------------------------

/**
 * @brief 跑体检（已按开关过滤）
 *
 * @param text 草稿正文
 * @return [{ruleId, severity, start, length, message}]
 */
QVariantList ToolsFacade::runChecks(const QString &text) const
{
    QVariantList result;
    const QStringList disabled = m_store.disabledCheckIds();
    const QList<PromptCheck> hits = PromptChecks::run(text);
    for (const PromptCheck &check : hits) {
        if (disabled.contains(check.ruleId)) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("ruleId"), check.ruleId);
        entry.insert(QStringLiteral("severity"), check.severity);
        entry.insert(QStringLiteral("start"), check.start);
        entry.insert(QStringLiteral("length"), check.length);
        // 建议文案在纯层以 QT_TRANSLATE_NOOP 登记、这里按同一上下文
        // 求值——QML 拿到的已是当前语言（纯数据没法 qsTr）。
        entry.insert(QStringLiteral("message"),
                     QCoreApplication::translate(
                         "PromptChecks",
                         check.message.toUtf8().constData()));
        result.append(entry);
    }
    return result;
}

/**
 * @brief 规则显示名
 */
QString ToolsFacade::checkRuleTitle(const QString &ruleId) const
{
    return PromptChecks::ruleTitle(ruleId);
}

/**
 * @brief 全部规则 id（稳定顺序）
 */
QStringList ToolsFacade::checkRuleIds() const
{
    return PromptChecks::ruleIds();
}

/**
 * @brief 估算 token 数
 */
int ToolsFacade::estimateTokens(const QString &text) const
{
    return ContextBudget::estimateTokens(text);
}

/**
 * @brief 预算分级（0/1/2，窗口基数取本门面的 contextWindowTokens）
 */
int ToolsFacade::budgetLevel(int tokens) const
{
    return ContextBudget::level(tokens,
                                m_store.editor().contextWindowTokens);
}

} // namespace awb::tools
