#include "markdown/MarkdownPreviewController.h"

#include "core/JsonStore.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "markdown/MarkdownRuntime.h"
#include "theme/Theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace awb::markdown {

namespace {

// 块 → 页面用的紧凑对象：i=索引 s=首行 e=末行 h=键 t=原文
QJsonObject blockJson(const MarkdownBlock &block)
{
    QJsonObject object;
    object.insert(QStringLiteral("i"), block.index);
    object.insert(QStringLiteral("s"), block.startLine);
    object.insert(QStringLiteral("e"), block.endLine);
    object.insert(QStringLiteral("h"), block.hash);
    object.insert(QStringLiteral("t"), block.text);
    return object;
}

QJsonArray blocksJson(const QVector<MarkdownBlock> &blocks)
{
    QJsonArray array;
    for (const MarkdownBlock &block : blocks) {
        array.append(blockJson(block));
    }
    return array;
}

// 可翻译串一律写成 QCoreApplication::translate("MarkdownPreviewController",
// "字面量")：套一层 source(text) 助手会让 lupdate 从形参里抽不到串，
// 界面就永远拿不到译文。裸类名与 lupdate 记的上下文一致（AGENTS.md i18n）。

} // namespace

/**
 * @brief 一个预览会话的全部状态
 *
 * 会话 = 一份文档（磁盘文件或虚拟的草稿文本）+ 它的块序列、大纲、rev、
 * 三态模式与导出落点。docId 是资源寻址键：doc.json 的载荷、asset/<docId>/
 * 的相对资源都按它对上。载荷缓存跟着会话走——切走再切回来，同 rev 的
 * doc.json 不必重新序列化。
 */
struct MarkdownPreviewController::Session {
    explicit Session(const QString &id)
        : docId(id)
    {
    }

    QString docId;                  ///< 资源寻址 id（文件会话 dN，草稿固定 "draft"）
    MarkdownDocument document;      ///< 文档本体（含 watcher 与字节形态）
    QVector<MarkdownBlock> blocks;  ///< 块序列（增量渲染的依据）
    QVariantList outline;           ///< 由块序列派生的大纲
    int rev = 0;                    ///< 会话内容版本，进 doc.json 的 query
    QString mode;                   ///< 三态模式（每会话一份）
    bool changedOnDisk = false;     ///< 外部改动提示的当前状态
    QString exportDirectory;        ///< 导出目录；空 = 文档所在目录
    mutable int cachedRev = -1;     ///< 载荷缓存对应的 rev
    mutable QString cachedBody;     ///< doc.json 载荷缓存（按 rev 失效）
};

/**
 * @brief 构造门面：接好主题通知线，并从设置里恢复视图偏好
 *
 * @param settings 视图偏好的落点（不拥有）
 * @param theme CSS 变量的来源（不拥有）
 * @param parent 父对象
 */
MarkdownPreviewController::MarkdownPreviewController(core::Settings *settings,
                                                     theme::Theme *theme,
                                                     QObject *parent)
    : QObject(parent),
      m_settings(settings),
      m_theme(theme)
{
    loadView();

    // 主题换色不能重建文档：只把新的 CSS 变量送进页面。这条线与会话无关
    //（页面只有一个，主题是全局的）。
    if (m_theme) {
        connect(m_theme, &theme::Theme::changed, this, [this]() {
            QVariantMap args;
            args.insert(QStringLiteral("vars"),
                        MarkdownRuntime::cssVariables(m_theme));
            args.insert(QStringLiteral("variant"), m_theme->variant());
            enqueue(QStringLiteral("setTheme"), args);
        });
    }
}

MarkdownPreviewController::~MarkdownPreviewController() = default;

/**
 * @brief 路径是否认作 Markdown（纯后缀判定，与能力无关）
 * @param path 磁盘路径
 * @return 后缀认作 Markdown 时为真
 *
 * 判定放在这一层而不是 QML：菜单项的可用态、openFile() 的拒绝、命令行
 * 参数的文件关联、以及将来 agent 报告的批量预览都要同一个答案。
 */
bool MarkdownPreviewController::isMarkdownPath(const QString &path)
{
    static const QRegularExpression re(
        QStringLiteral("\\.(md|markdown|mdown|mdtxt|mdtext|text)\\z"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(path).hasMatch();
}

/**
 * @brief 本构建有没有预览能力
 * @return 有则为真
 *
 * 两个条件缺一不可：编译期有 WebEngine 与资源，运行期适配器真的把
 * awbmd:// 的表面装上了。少后者会让菜单在「scheme 注册顺序写错」这种缺陷里
 * 依然可点，而用户看到的是一篇能打开、公式与图表却永远出不来的文档。
 */
bool MarkdownPreviewController::available() const
{
    return m_surfaceReady && MarkdownRuntime::isSupportedBuild();
}

/**
 * @brief 没有能力时的解释
 * @return 英文源串；有能力时为空
 */
QString MarkdownPreviewController::unavailableReason() const
{
    if (available()) {
        return QString();
    }
    if (!MarkdownRuntime::isSupportedBuild()) {
        return MarkdownRuntime::unavailableReason();
    }
    // 走到这里说明这个二进制本该有预览、却没有表面——是接线错了，
    // 不是用户的构建问题，但界面仍要给出一个字，不能显示空原因。
    return QCoreApplication::translate(
        "MarkdownPreviewController",
        "Markdown preview is unavailable: the preview surface is not "
        "installed.");
}

/**
 * @brief 适配器报告预览表面就绪
 * @param ready 表面已装上为真
 *
 * 由 MarkdownPreviewProvider 在装好 scheme handler 之后调用一次。变化要惊动
 * 所有盯 available 的地方（菜单项与窗口 Loader）。
 */
void MarkdownPreviewController::setSurfaceReady(bool ready)
{
    if (m_surfaceReady == ready) {
        return;
    }
    m_surfaceReady = ready;
    Q_EMIT capabilitiesChanged();
}

/**
 * @brief 渲染栈版本串
 * @return 展示用字符串，可能为空
 */
QString MarkdownPreviewController::rendererVersion() const
{
    return MarkdownRuntime::rendererVersionText();
}

// --- 会话管理 ------------------------------------------------------------

/**
 * @brief 按 docId 找会话
 * @param docId 资源寻址 id
 * @return 文件会话或草稿会话；找不到为空
 */
std::shared_ptr<MarkdownPreviewController::Session>
MarkdownPreviewController::sessionById(const QString &docId) const
{
    for (const auto &session : m_drafts) {
        if (session->docId == docId) {
            return session;
        }
    }
    for (const auto &session : m_sessions) {
        if (session->docId == docId) {
            return session;
        }
    }
    return nullptr;
}

/**
 * @brief 这个 docId 是否某个草稿会话
 */
bool MarkdownPreviewController::isDraftId(const QString &docId) const
{
    for (const auto &session : m_drafts) {
        if (session->docId == docId) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 活跃会话的裸指针（读侧专用）
 * @return 活跃会话；无会话时为空
 */
MarkdownPreviewController::Session *
MarkdownPreviewController::activeSession() const
{
    return m_active.get();
}

/**
 * @brief 接好一个会话的文档信号线
 * @param session 新建的会话
 *
 * 每条线都先问「它是不是活跃会话」再往外广播：非活跃会话的翻转只落在
 * 会话自己的状态里（切回来时属性自然是对的），不发全局信号——否则关一个
 * 无关 tab 的脏标记会把正在看的文档状态搅一遍。
 * 捕获裸指针是安全的：信号线以会话的文档为 sender，会话销毁（sender 析构）
 * 连接随之断开，lambda 没有机会带着悬垂指针被调到。
 */
void MarkdownPreviewController::wireSessionSignals(
    const std::shared_ptr<Session> &session)
{
    Session *const raw = session.get();
    connect(&raw->document, &MarkdownDocument::dirtyChanged, this,
            [this, raw]() {
                if (m_active.get() == raw) {
                    Q_EMIT dirtyChanged();
                    Q_EMIT tabsChanged();
                }
            });
    connect(&raw->document, &MarkdownDocument::readOnlyChanged, this,
            [this, raw]() {
                if (m_active.get() == raw) {
                    Q_EMIT readOnlyChanged();
                }
            });
    // 外部改动只记会话状态，怎么问用户是窗口的事。监视器与轮询复查共用
    // 同一个入口，两条路的结论必须一致。
    connect(&raw->document, &MarkdownDocument::externallyChangedByOther,
            this, [this, raw]() { refreshDiskStateFor(raw); });
}

/**
 * @brief 建一个新文件会话（读盘 + 切块 + 接信号线）
 * @param path 磁盘路径
 * @param failure 输出：读盘失败的原因（成功时不清空）
 * @return 新会话；读盘失败为空（此时**不**入列，既有会话不受影响）
 */
std::shared_ptr<MarkdownPreviewController::Session>
MarkdownPreviewController::createFileSession(const QString &path,
                                             core::OpResult *failure)
{
    auto session = std::make_shared<Session>(
        QStringLiteral("d%1").arg(++m_docSeq));
    const core::OpResult loaded = session->document.load(path);
    if (!loaded) {
        if (failure) {
            *failure = loaded;
        }
        return nullptr;
    }
    session->blocks = MarkdownChunker::split(session->document.text());
    session->outline = MarkdownRuntime::outlineFromBlocks(session->blocks);
    session->rev = 1;
    session->mode = m_defaultMode;
    wireSessionSignals(session);

    AWB_PERF << "markdown: opened" << session->document.fileName()
             << session->document.byteSize() << "bytes ->"
             << session->blocks.size() << "blocks";
    return session;
}

/**
 * @brief 打开一份 Markdown 文档
 * @param path 磁盘路径
 * @return 成功，或带原因的失败
 *
 * 同一路径已有会话时只激活它（tab 已在条上，未保存的改动也还在）；否则
 * 建新会话并激活。失败一律不开窗、不动既有会话：菜单点错了不该把用户
 * 正在看的那份弄没。
 */
core::OpResult MarkdownPreviewController::openFile(const QString &path)
{
    if (!available()) {
        return core::OpResult::failure(unavailableReason());
    }
    if (!isMarkdownPath(path)) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownPreviewController",
            "Only Markdown files can be previewed here."));
    }
    // 文件会话上限（小决策 13）：满了就明说，而不是静默挤掉最久没看的
    // tab——用户正在改的东西不该因为开了第 9 份文档而消失。
    if (m_sessions.size() >= kMaxFileSessions) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownPreviewController",
            "Too many documents are open. Close a tab before opening "
            "another one."));
    }

    // 同路径去重：QFileInfo 两侧都取绝对路径，`.`/`..`/大小写盘符的写法
    // 差异不该开出第二个 tab。
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (const auto &session : m_sessions) {
        if (session->document.path() == absolute) {
            activateSession(session);
            Q_EMIT windowRequested();
            return core::OpResult::success();
        }
    }

    core::OpResult failure;
    std::shared_ptr<Session> session = createFileSession(path, &failure);
    if (!session) {
        return failure;
    }
    m_sessions.append(session);
    activateSession(session);
    Q_EMIT windowRequested();
    return core::OpResult::success();
}

/**
 * @brief 激活一个会话（QML 入口）
 * @param docId 会话 id
 */
void MarkdownPreviewController::activateSession(const QString &docId)
{
    const std::shared_ptr<Session> session = sessionById(docId);
    if (!session) {
        return;
    }
    activateSession(session);
}

/**
 * @brief 激活切换的公共路径
 * @param session 目标会话
 * @return 真的换人了为真（已是活跃会话或空会话时为假）
 *
 * 换会话 = 渲染面整篇重建。窗口开着时发一条 setDocument（页面据此重新
 * fetch doc.json）；窗口没开就不必发——将来窗口的装载流程本来就会取活跃
 * 会话的那份。
 */
bool MarkdownPreviewController::activateSession(
    const std::shared_ptr<Session> &session)
{
    if (!session || session == m_active) {
        return false;
    }
    m_active = session;
    if (m_windowOpen) {
        QVariantMap args;
        args.insert(QStringLiteral("rev"), session->rev);
        enqueue(QStringLiteral("setDocument"), args);
    }
    Q_EMIT activeSessionChanged();
    Q_EMIT documentChanged();
    Q_EMIT outlineChanged();
    Q_EMIT dirtyChanged();
    Q_EMIT readOnlyChanged();
    Q_EMIT changedOnDiskChanged();
    Q_EMIT viewChanged();
    Q_EMIT tabsChanged();
    return true;
}

/**
 * @brief 关掉一个文件会话
 * @param docId 会话 id
 *
 * 关 tab ≠ 删文档：磁盘文件不动，只是视图收起来。关的是活跃会话时顺位
 * 激活最后打开的文件会话，没有文件会话则退到最近创建的草稿。草稿会话
 * 由 closeDraftSession() 负责（关标签 ≠ 删草稿，同一语义的两半）。
 */
void MarkdownPreviewController::closeSession(const QString &docId)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i)->docId != docId) {
            continue;
        }
        const bool wasActive = m_sessions.at(i) == m_active;
        m_sessions.removeAt(i);
        if (wasActive) {
            m_active.reset();
            if (!m_sessions.isEmpty()) {
                activateSession(m_sessions.constLast());
            } else if (!m_drafts.isEmpty()) {
                activateSession(m_drafts.constLast());
            } else {
                // 一个会话都不剩：把「无文档」的空态广播出去
                Q_EMIT activeSessionChanged();
                Q_EMIT documentChanged();
                Q_EMIT outlineChanged();
                Q_EMIT dirtyChanged();
                Q_EMIT readOnlyChanged();
                Q_EMIT changedOnDiskChanged();
                Q_EMIT viewChanged();
            }
        }
        Q_EMIT tabsChanged();
        return;
    }
}

/**
 * @brief 关掉一个草稿会话
 * @param draftId 草稿 id（即 docId）
 *
 * 关 tab ≠ 删草稿：tools.json 的记录不动，只是视图收起来。关的是活跃
 * 会话时顺位：最近的草稿 → 最后打开的文件会话 → 空态。
 */
void MarkdownPreviewController::closeDraftSession(const QString &draftId)
{
    for (int i = 0; i < m_drafts.size(); ++i) {
        if (m_drafts.at(i)->docId != draftId) {
            continue;
        }
        const bool wasActive = m_drafts.at(i) == m_active;
        m_drafts.removeAt(i);
        if (wasActive) {
            m_active.reset();
            if (i < m_drafts.size()) {
                activateSession(m_drafts.at(i));
            } else if (!m_drafts.isEmpty()) {
                activateSession(m_drafts.constLast());
            } else if (!m_sessions.isEmpty()) {
                activateSession(m_sessions.constLast());
            } else {
                Q_EMIT activeSessionChanged();
                Q_EMIT documentChanged();
                Q_EMIT outlineChanged();
                Q_EMIT dirtyChanged();
                Q_EMIT readOnlyChanged();
                Q_EMIT changedOnDiskChanged();
                Q_EMIT viewChanged();
            }
        }
        Q_EMIT tabsChanged();
        return;
    }
}

/**
 * @brief 建一个草稿会话（幂等）
 * @param draftId tools.json 里那条草稿的 id（docId 与之相同）
 * @param title 草稿标题（tab 与渲染标题用）
 * @param body 草稿正文
 * @return docId（= draftId）；draftId 空时返回空串
 *
 * 草稿是虚拟文档：编辑面在 tools 域的草稿编辑器（tools.json 持久化），
 * 这里只做渲染镜像。没有预览能力的构建里也可以建（窗口的预览面不存在，
 * 会话只是闲置状态，不给任何路径添麻烦）。
 */
QString MarkdownPreviewController::openDraftSession(const QString &draftId,
                                                    const QString &title,
                                                    const QString &body)
{
    if (draftId.isEmpty()) {
        return QString();
    }
    for (const auto &existing : m_drafts) {
        if (existing->docId == draftId) {
            return existing->docId;   // 幂等
        }
    }
    auto session = std::make_shared<Session>(draftId);
    session->document.setVirtual(title, body);
    session->blocks = MarkdownChunker::split(body);
    session->outline = MarkdownRuntime::outlineFromBlocks(session->blocks);
    session->rev = 1;
    // 草稿的默认三态不是文件的默认：草稿是用来**写**的，分栏（写 +
    // 看）是它的自然形态；preview 起步会把编辑器整个藏掉。无预览能力
    // 的构建退到纯源码态。这个初值不落 settings——用户改模式时才
    // 更新默认（setMode）。
    session->mode = available() ? QStringLiteral("split")
                                : QStringLiteral("source");
    wireSessionSignals(session);
    m_drafts.append(session);
    Q_EMIT tabsChanged();
    return session->docId;
}

/**
 * @brief 草稿标题变了（重命名后刷新 tab 与渲染标题）
 */
void MarkdownPreviewController::setDraftTitle(const QString &draftId,
                                              const QString &title)
{
    for (const auto &session : m_drafts) {
        if (session->docId == draftId) {
            if (session->document.fileName() != title) {
                session->document.setVirtual(title,
                                              session->document.text());
                Q_EMIT tabsChanged();
                if (m_active == session) {
                    Q_EMIT documentChanged();
                }
            }
            return;
        }
    }
}

/**
 * @brief 草稿正文回写
 * @param draftId 草稿 id
 * @param text 草稿全文
 *
 * 与文件会话的 setEditorText 同一条路（重切块 + 差异增量）；区别只在
 * 虚拟文档永不脏、永不存盘。窗口的草稿编辑器每敲一键调一次。会话不
 * 存在时忽略（窗口动作层先 openDraftSession 建好再喂文本）。
 */
void MarkdownPreviewController::setDraftText(const QString &draftId,
                                             const QString &text)
{
    const auto session = sessionById(draftId);
    if (!session || !isDraftId(draftId)) {
        return;
    }
    applySessionText(session.get(), text);
}

/**
 * @brief 文件会话的 tab 清单
 * @return [{docId, title, path, mode, dirty, active, draft}]
 *
 * 草稿 tab 排在文件 tab 之前（创建序）；文件 tab 按打开顺序。path 与
 * mode 供 workbench 域的会话文件恢复 tab 组（草稿按 id、文件按路径）；
 * draft 为真表示草稿 tab（path 为空串）——界面用它画来源徽标。
 */
QVariantList MarkdownPreviewController::tabs() const
{
    QVariantList result;
    for (const auto &session : m_drafts) {
        QVariantMap entry;
        entry.insert(QStringLiteral("docId"), session->docId);
        entry.insert(QStringLiteral("title"), session->document.fileName());
        entry.insert(QStringLiteral("path"), QString());
        entry.insert(QStringLiteral("mode"), session->mode);
        entry.insert(QStringLiteral("dirty"), false);
        entry.insert(QStringLiteral("active"), session == m_active);
        entry.insert(QStringLiteral("draft"), true);
        result.append(entry);
    }
    for (const auto &session : m_sessions) {
        QVariantMap entry;
        entry.insert(QStringLiteral("docId"), session->docId);
        entry.insert(QStringLiteral("title"), session->document.fileName());
        entry.insert(QStringLiteral("path"), session->document.path());
        entry.insert(QStringLiteral("mode"), session->mode);
        entry.insert(QStringLiteral("dirty"), session->document.dirty());
        entry.insert(QStringLiteral("active"), session == m_active);
        entry.insert(QStringLiteral("draft"), false);
        result.append(entry);
    }
    return result;
}

/**
 * @brief 活跃会话的 docId
 * @return 会话 id；无活跃会话时为空串
 */
QString MarkdownPreviewController::activeDocId() const
{
    return m_active ? m_active->docId : QString();
}

/**
 * @brief 活跃会话是否是草稿
 */
bool MarkdownPreviewController::isDraftActive() const
{
    return m_active && isDraftId(m_active->docId);
}

/**
 * @brief 活跃草稿的 id（docId 即草稿 id）；非草稿活跃时空串
 */
QString MarkdownPreviewController::activeDraftId() const
{
    if (!m_active || !isDraftId(m_active->docId)) {
        return QString();
    }
    return m_active->docId;
}

// --- 反映活跃会话的属性读侧 ---------------------------------------------

QString MarkdownPreviewController::filePath() const
{
    return activeSession() ? activeSession()->document.path() : QString();
}

QString MarkdownPreviewController::fileTitle() const
{
    return activeSession() ? activeSession()->document.fileName() : QString();
}

bool MarkdownPreviewController::dirty() const
{
    return activeSession() ? activeSession()->document.dirty() : false;
}

bool MarkdownPreviewController::readOnly() const
{
    return activeSession() ? activeSession()->document.readOnly() : false;
}

QString MarkdownPreviewController::readOnlyReason() const
{
    return activeSession() ? activeSession()->document.readOnlyReason()
                           : QString();
}

bool MarkdownPreviewController::canEdit() const
{
    return activeSession() ? activeSession()->document.canEdit() : false;
}

bool MarkdownPreviewController::splitAvailable() const
{
    return canEdit();
}

bool MarkdownPreviewController::changedOnDisk() const
{
    return activeSession() ? activeSession()->changedOnDisk : false;
}

qint64 MarkdownPreviewController::byteSize() const
{
    return activeSession() ? activeSession()->document.byteSize() : 0;
}

QString MarkdownPreviewController::mode() const
{
    return activeSession() ? activeSession()->mode : m_defaultMode;
}

QVariantList MarkdownPreviewController::outline() const
{
    return activeSession() ? activeSession()->outline : QVariantList();
}

const QVector<MarkdownBlock> &MarkdownPreviewController::blocks() const
{
    if (activeSession()) {
        return activeSession()->blocks;
    }
    static const QVector<MarkdownBlock> empty;
    return empty;
}

/**
 * @brief 预览页面的地址
 * @return awbmd://runtime/app/shell.html
 *
 * 视图的 url 由 QML 绑到这里，而不是在 QML 里拼字符串：scheme 名、host 与
 * 资源前缀的定义只有 MarkdownRuntime 一处。
 */
QUrl MarkdownPreviewController::pageUrl() const
{
    return MarkdownRuntime::appUrl(QStringLiteral("shell.html"));
}

/**
 * @brief 这个路径是否归预览管
 * @param path 磁盘路径
 * @return 本构建有预览能力且后缀认作 Markdown 时为真
 *
 * 菜单项用它决定出不出场，openFile() 用它挡下别的文件——同一个答案只能有
 * 一处来源，否则「菜单里能点、点开被拒」这种分裂迟早出现。
 */
bool MarkdownPreviewController::canPreviewPath(const QString &path) const
{
    return available() && isMarkdownPath(path);
}

/**
 * @brief 窗口已建立，后续命令要真的发出去了
 */
void MarkdownPreviewController::notifyWindowOpened()
{
    if (m_windowOpen) {
        return;
    }
    m_windowOpen = true;
    Q_EMIT activeChanged();
}

/**
 * @brief 窗口关闭：放下在飞命令，**会话全部保留**
 *
 * 会话是应用运行期的记忆：重开窗口时 doc.json 直接取回活跃会话，各 tab 的
 * 状态（含未保存改动）原样还在——「关窗重开找回上次的 tab 组」在运行期内
 * 因此是免费的。跨启动的恢复由 workbench 域的会话文件负责（那里只有路径
 * 与 tab 结构，没有正文）。
 */
void MarkdownPreviewController::notifyWindowClosed()
{
    if (!m_windowOpen) {
        return;
    }
    m_windowOpen = false;
    m_queue.clear();
    Q_EMIT activeChanged();
}

// --- 编辑与存盘（活跃会话） ----------------------------------------------

/**
 * @brief 编辑器回写正文（活跃会话）
 * @param text 新的全文
 *
 * 走「重切块 + 差异」而不是整篇重建：一次编辑通常只碰一两个块，把整篇
 * 再发一遍等于让页面把 2 MB 重排一次。
 */
void MarkdownPreviewController::setEditorText(const QString &text)
{
    Session *const session = activeSession();
    if (!session) {
        return;
    }
    applySessionText(session, text);
}

/**
 * @brief 活跃会话的正文
 * @return 全文；无会话时为空串
 */
QString MarkdownPreviewController::editorText() const
{
    return activeSession() ? activeSession()->document.text() : QString();
}

/**
 * @brief 把一段编辑回写进会话状态（文件与草稿共用）
 * @param session 目标会话
 * @param text 新的全文
 */
void MarkdownPreviewController::applySessionText(Session *session,
                                                 const QString &text)
{
    if (!session || session->document.text() == text) {
        return;
    }
    session->document.setText(text);
    const QVector<MarkdownBlock> next = MarkdownChunker::split(text);
    const MarkdownChunker::Diff change =
        MarkdownChunker::diff(session->blocks, next);
    session->blocks = next;
    const QVariantList nextOutline =
        MarkdownRuntime::outlineFromBlocks(session->blocks);
    const bool outlineDiffers = (nextOutline != session->outline);
    session->outline = nextOutline;
    ++session->rev;
    session->cachedRev = -1;

    const bool isActive = (m_active.get() == session);
    if (isActive) {
        Q_EMIT documentChanged();
        if (outlineDiffers) {
            Q_EMIT outlineChanged();
        }
    }
    if (isActive && m_windowOpen && !change.isNull()) {
        QJsonObject args;
        args.insert(QStringLiteral("rev"), session->rev);
        args.insert(QStringLiteral("startIndex"), change.startIndex);
        args.insert(QStringLiteral("removeCount"), change.removeCount);
        args.insert(QStringLiteral("blocks"), blocksJson(change.inserted));
        enqueue(QStringLiteral("updateBlocks"), args);
    }
}

/**
 * @brief 存盘（活跃会话）
 * @return 成功或失败原因
 */
core::OpResult MarkdownPreviewController::saveNow()
{
    Session *const session = activeSession();
    if (!session) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownPreviewController", "There is no file to save to."));
    }
    const core::OpResult written = session->document.save();
    if (written) {
        Q_EMIT saved();
        Q_EMIT tabsChanged();
    }
    return written;
}

/**
 * @brief 按 id 存某个文件会话，不改变激活
 * @param docId 会话 id
 * @return 成功或失败原因
 *
 * 关窗守卫的「全部保存」走它：逐个激活再存会把渲染面来回换八遍，而且
 * 存到一半失败时用户看到的「当前文档」已经不知道被换成了哪一个。
 */
core::OpResult MarkdownPreviewController::saveSession(const QString &docId)
{
    for (const auto &session : m_sessions) {
        if (session->docId != docId) {
            continue;
        }
        const core::OpResult written = session->document.save();
        if (written) {
            Q_EMIT saved();
            Q_EMIT tabsChanged();
        }
        return written;
    }
    return core::OpResult::failure(QCoreApplication::translate(
        "MarkdownPreviewController", "There is no file to save to."));
}

/**
 * @brief 放弃本地改动，重新读盘（活跃会话）
 * @return 成功或失败原因
 */
core::OpResult MarkdownPreviewController::reloadFromDisk()
{
    Session *const session = activeSession();
    if (!session || session->document.path().isEmpty()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownPreviewController", "There is no file to reload."));
    }
    const QString path = session->document.path();
    const core::OpResult loaded = session->document.load(path);
    if (!loaded) {
        return loaded;
    }
    rebuildSession(session);
    return core::OpResult::success();
}

/**
 * @brief 会话整体重算（块 + 大纲 + rev），并广播状态
 * @param session 目标会话（重载后调用）
 */
void MarkdownPreviewController::rebuildSession(Session *session)
{
    if (!session) {
        return;
    }
    session->blocks = MarkdownChunker::split(session->document.text());
    session->outline = MarkdownRuntime::outlineFromBlocks(session->blocks);
    ++session->rev;
    session->changedOnDisk = false;
    session->cachedRev = -1;

    if (m_active.get() != session) {
        return;
    }
    Q_EMIT documentChanged();
    Q_EMIT outlineChanged();
    Q_EMIT dirtyChanged();
    Q_EMIT changedOnDiskChanged();
    if (m_windowOpen) {
        QVariantMap args;
        args.insert(QStringLiteral("rev"), session->rev);
        enqueue(QStringLiteral("setDocument"), args);
    }
}

/**
 * @brief 用户选择保留自己的改动，提示消失（活跃会话）
 *
 * 必须同时把磁盘那份认作基线：只清状态位的话，下一次 refreshDiskState()
 * 会算出同样的差异、把提示再弹一遍，用户关不掉它。
 */
void MarkdownPreviewController::dismissDiskNotice()
{
    Session *const session = activeSession();
    if (!session || !session->changedOnDisk) {
        return;
    }
    session->document.acceptDiskState();
    session->changedOnDisk = false;
    Q_EMIT changedOnDiskChanged();
}

/**
 * @brief 重新比对磁盘形态，刷新 changedOnDisk（活跃会话）
 */
void MarkdownPreviewController::refreshDiskState()
{
    refreshDiskStateFor(activeSession());
}

/**
 * @brief 某个会话的磁盘差异复查
 * @param session 目标会话
 *
 * watcher 回调与窗口激活轮询两条路都汇到这里。无文档时差异一律为假：
 * 没有基线可比。
 */
void MarkdownPreviewController::refreshDiskStateFor(Session *session)
{
    if (!session) {
        return;
    }
    const bool changed =
        session->document.hasPath() && session->document.externallyChanged();
    if (changed == session->changedOnDisk) {
        return;
    }
    session->changedOnDisk = changed;
    if (m_active.get() == session) {
        Q_EMIT changedOnDiskChanged();
    }
}

// --- 视图设置 -----------------------------------------------------------

/**
 * @brief 三态模式（每会话一份）
 * @param mode preview / split / source；其它取值忽略
 *
 * 会话自己的模式当场换；settings 里的那份同步成「新会话的默认值」。
 */
void MarkdownPreviewController::setMode(const QString &mode)
{
    if (mode != QStringLiteral("preview") && mode != QStringLiteral("split")
        && mode != QStringLiteral("source")) {
        return;
    }
    Session *const session = activeSession();
    if (!session) {
        if (m_defaultMode == mode) {
            return;
        }
        m_defaultMode = mode;
        persistView(mode);
        Q_EMIT viewChanged();
    }
    else {
        if (session->mode == mode) {
            return;
        }
        session->mode = mode;
        session->cachedRev = -1;
        m_defaultMode = mode;
        persistView(mode);
        Q_EMIT viewChanged();
    }
    QVariantMap args;
    args.insert(QStringLiteral("mode"), mode);
    args.insert(QStringLiteral("canEdit"), canEdit());
    enqueue(QStringLiteral("setView"), args);
}

/**
 * @brief 正文字号
 * @param pixels 像素；0 = 跟随主题
 */
void MarkdownPreviewController::setFontSizePx(int pixels)
{
    const int clamped = qBound(0, pixels, 48);
    if (m_fontSizePx == clamped) {
        return;
    }
    m_fontSizePx = clamped;
    persistView(mode());
    invalidateAllSessionBodies();
    Q_EMIT viewChanged();
    QVariantMap args;
    args.insert(QStringLiteral("fontSizePx"), clamped);
    enqueue(QStringLiteral("setView"), args);
}

/**
 * @brief 正文栏宽
 * @param pixels 像素；0 = 内置基准
 */
void MarkdownPreviewController::setContentWidthPx(int pixels)
{
    const int clamped = qBound(0, pixels, 4000);
    if (m_contentWidthPx == clamped) {
        return;
    }
    m_contentWidthPx = clamped;
    persistView(mode());
    invalidateAllSessionBodies();
    Q_EMIT viewChanged();
    QVariantMap args;
    args.insert(QStringLiteral("contentWidthPx"), clamped);
    enqueue(QStringLiteral("setView"), args);
}

/**
 * @brief 大纲侧栏开关
 * @param show 显示为真
 */
void MarkdownPreviewController::setShowOutline(bool show)
{
    if (m_showOutline == show) {
        return;
    }
    m_showOutline = show;
    persistView(mode());
    invalidateAllSessionBodies();
    Q_EMIT viewChanged();
    QVariantMap args;
    args.insert(QStringLiteral("showOutline"), show);
    enqueue(QStringLiteral("setView"), args);
}

/**
 * @brief 「启动时自动打开工作台」开关
 * @param open 启动即开窗为真
 *
 * 只是设置项的搬运（页面开关 ↔ settings.json），与会话状态无关。
 */
void MarkdownPreviewController::setAutoOpenWorkbench(bool open)
{
    if (m_autoOpenWorkbench == open) {
        return;
    }
    m_autoOpenWorkbench = open;
    if (m_settings) {
        m_settings->setMarkdownAutoOpenWorkbench(open);
        m_settings->save();
    }
    Q_EMIT viewChanged();
}

/**
 * @brief 让每个会话的 doc.json 载荷缓存失效
 *
 * 字号/栏宽/大纲开关进**每一份**载荷：改了它们，任何会话切回来时页面
 * 重新 fetch 都必须拿到新值，缓存只按会话自己的 rev 失效就不够了。
 */
void MarkdownPreviewController::invalidateAllSessionBodies()
{
    for (const auto &session : m_sessions) {
        session->cachedRev = -1;
    }
    for (const auto &session : m_drafts) {
        session->cachedRev = -1;
    }
}

// --- doc.json 载荷与资源寻址 --------------------------------------------

/**
 * @brief 页面 fetch('doc.json') 的响应体（活跃会话）
 * @return 整篇文档 + 块 + 视图 + 主题 + 文案的 JSON
 *
 * 按 rev 缓存在会话上：一次渲染周期里页面可能取多次（首屏、字体切换后的
 * 重取），每次重新序列化 2 MB 文本是白干。缓存只在会话自己的 rev 变化或
 * 全局视图偏好变化时失效。
 */
QString MarkdownPreviewController::documentBodyJson() const
{
    const Session *const session = activeSession();
    if (!session) {
        return QStringLiteral("{}");
    }
    if (session->cachedRev == session->rev && !session->cachedBody.isEmpty()) {
        return session->cachedBody;
    }

    QJsonObject view;
    view.insert(QStringLiteral("mode"), session->mode);
    view.insert(QStringLiteral("fontSizePx"), m_fontSizePx);
    view.insert(QStringLiteral("contentWidthPx"), m_contentWidthPx);
    view.insert(QStringLiteral("showOutline"), m_showOutline);
    view.insert(QStringLiteral("canEdit"), canEdit());

    QJsonObject root;
    root.insert(QStringLiteral("rev"), session->rev);
    root.insert(QStringLiteral("docId"), session->docId);
    root.insert(QStringLiteral("path"), session->document.path());
    root.insert(QStringLiteral("title"), session->document.fileName());
    root.insert(QStringLiteral("directory"), session->document.directory());
    root.insert(QStringLiteral("bytes"),
                double(session->document.byteSize()));
    root.insert(QStringLiteral("readOnly"), readOnly());
    root.insert(QStringLiteral("readOnlyReason"),
                session->document.readOnlyReason());
    root.insert(QStringLiteral("assetBase"),
                MarkdownRuntime::assetUrl(session->docId,
                                          QStringLiteral("")).toString());
    root.insert(QStringLiteral("view"), view);
    root.insert(QStringLiteral("vars"),
                QJsonObject::fromVariantMap(
                    MarkdownRuntime::cssVariables(m_theme)));
    root.insert(QStringLiteral("strings"), MarkdownRuntime::pageStrings());
    root.insert(QStringLiteral("blocks"), blocksJson(session->blocks));
    root.insert(QStringLiteral("outline"),
                QJsonArray::fromVariantList(session->outline));

    const QString body = QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    // const 方法要写缓存：这两支是页面侧的唯一真相，缓存错了就是渲染错了。
    session->cachedRev = session->rev;
    session->cachedBody = body;
    return body;
}

/**
 * @brief 活跃会话的 docId
 */
QString MarkdownPreviewController::docId() const
{
    return activeSession() ? activeSession()->docId : QString();
}

/**
 * @brief 活跃会话的文档目录
 */
QString MarkdownPreviewController::assetRoot() const
{
    return activeSession() ? activeSession()->document.directory()
                           : QString();
}

/**
 * @brief 任意活着会话的文档目录
 * @param docId 资源寻址 id
 * @return 文档目录；会话不存在或没有目录（草稿）时为空串
 *
 * 适配器按 asset/<docId>/… 寻址：换页瞬间旧会话的图片请求还可能在飞，
 * 所以解析范围是**所有**活着的会话，不只是活跃的那个。
 */
QString MarkdownPreviewController::assetRootFor(const QString &docId) const
{
    const std::shared_ptr<Session> session = sessionById(docId);
    return session ? session->document.directory() : QString();
}

// --- 命令队列 ------------------------------------------------------------

/**
 * @brief 取出队首命令
 * @return 命令 JSON；队列为空时返回空串
 */
QString MarkdownPreviewController::takeCommand()
{
    if (m_queue.isEmpty()) {
        return QString();
    }
    const QString command = m_queue.constFirst().second;
    m_queue.removeFirst();
    return command;
}

/**
 * @brief 页面就绪
 * @param json {page,rev,blocks,firstPaintMs,contentVisibility,fetchApi}
 *
 * 就绪即把视图与主题两条命令补发一次：doc.json 里已经带了它们，这条只是
 * 让「页面加载完成」与「设置命令到达」的顺序无关紧要。
 */
void MarkdownPreviewController::reportReady(const QString &json)
{
    AWB_PERF << "markdown: page ready" << json.left(200);
    QVariantMap args;
    args.insert(QStringLiteral("mode"), mode());
    args.insert(QStringLiteral("fontSizePx"), m_fontSizePx);
    args.insert(QStringLiteral("contentWidthPx"), m_contentWidthPx);
    args.insert(QStringLiteral("showOutline"), m_showOutline);
    args.insert(QStringLiteral("canEdit"), canEdit());
    enqueue(QStringLiteral("setView"), args);
    QVariantMap themeArgs;
    themeArgs.insert(QStringLiteral("vars"),
                     MarkdownRuntime::cssVariables(m_theme));
    themeArgs.insert(QStringLiteral("variant"),
                     m_theme ? m_theme->variant() : QStringLiteral("dark"));
    enqueue(QStringLiteral("setTheme"), themeArgs);
}

/**
 * @brief 页面报回的一次渲染耗时
 * @param json {parseMs,mountMs,passes,blockCount,mountedCount,pendingBlocks,
 *              pendingDiagrams,totalMs}
 *
 * 只进日志，不进界面；这条是 docs/research/markdown-preview-runtime.md 里那些
 * 数字的来源。两个口径要分开看：parseMs+mountMs 是渲染实际做的事，totalMs 是
 * 用户等的那段时间，passes 说明中间让出了多少次主线程。
 */
void MarkdownPreviewController::reportFrame(const QString &json)
{
    AWB_PERF << "markdown: frame" << json.left(320);
}

/**
 * @brief 页面报回的一次渲染失败
 * @param json {blockIndex, message}
 */
void MarkdownPreviewController::reportError(const QString &json)
{
    qWarning().noquote() << "markdown: page reported" << json.left(400);
    Q_EMIT renderFailed(json);
}

/**
 * @brief 页面把整篇序列化好的 HTML 交回来，这里负责落盘
 * @param html 自包含的 HTML 文本
 *
 * purpose 由 requestMarkup() 记住：一次只可能有一个序列化请求在飞（按钮
 * 点了会等结果回来才再可用），所以不需要把 id 传进传出。
 *
 * 两条路都写盘，区别只在写到哪（落点取自**活跃会话**）：
 *  - "export" → exportDirectory()（空 = 文档所在目录）下的 exportFileName()。
 *    图片在导出件里是**相对引用**，落在文档旁边才显示得出来，这是默认目录的
 *    理由，也是用户换目录导出时要自己承担的后果。
 *  - "browser" → <dataRoot>/preview 下的同名文件。外部浏览器不认识 awbmd://，
 *    也没有文档目录的概念，只能给它一份真实的盘上文件。
 * QML 侧不做文件 IO（AGENTS.md），所以目标位置在 requestMarkup() 之前就经
 * setExportDirectory() 定好，这里只负责写与报结果。
 */
void MarkdownPreviewController::reportMarkup(const QString &html)
{
    const QString purpose = m_markupPurpose;
    m_markupPurpose.clear();
    Q_EMIT markupReady(html, purpose);

    const Session *const session = activeSession();
    const QByteArray bytes = html.toUtf8();
    QString target;
    if (purpose == QLatin1String("browser")) {
        QDir().mkpath(core::Paths::markdownPreviewDir());
        target = core::Paths::markdownPreviewDir() + QLatin1Char('/')
                + exportFileName();
    } else {
        const QString directory = (session && !session->exportDirectory.isEmpty())
                ? session->exportDirectory
                : (session ? session->document.directory() : QString());
        if (directory.isEmpty()) {
            Q_EMIT markupWriteFailed(purpose, QCoreApplication::translate(
                "MarkdownPreviewController",
                "There is no folder to export into: open a saved file first, "
                "or pick a folder."));
            return;
        }
        target = directory + QLatin1Char('/') + exportFileName();
    }

    const core::OpResult written = core::JsonStore::writeBytes(target, bytes);
    if (!written) {
        Q_EMIT markupWriteFailed(purpose, written.error);
        return;
    }
    AWB_PERF << "markdown: wrote markup" << target << bytes.size() << "bytes";
    Q_EMIT markupWritten(target, purpose);
}

/**
 * @brief 记下导出目录（活跃会话）
 * @param directory 绝对路径；空串恢复默认（文档所在目录）
 */
void MarkdownPreviewController::setExportDirectory(const QString &directory)
{
    Session *const session = activeSession();
    if (!session) {
        return;
    }
    session->exportDirectory = directory;
}

/**
 * @brief 导出目录（活跃会话）
 */
QString MarkdownPreviewController::exportDirectory() const
{
    return activeSession() ? activeSession()->exportDirectory : QString();
}

/**
 * @brief 导出文件的建议名
 * @return 文档主名换 .html；没有文档时 "Markdown.html"
 *
 * 名字里带的是**当前文档**的主名，与内容脏不脏无关：导出件本来就是当前屏幕
 * 上那份渲染结果，用户要的「把这份 README 导出去」不该得到一个 preview.html。
 */
QString MarkdownPreviewController::exportFileName() const
{
    const QString source = fileTitle();
    if (source.isEmpty()) {
        return QStringLiteral("Markdown.html");
    }
    const int dot = source.lastIndexOf(QLatin1Char('.'));
    return (dot > 0 ? source.left(dot) : source) + QStringLiteral(".html");
}

/**
 * @brief 请求整篇序列化
 * @param purpose "export" 或 "browser"，决定 reportMarkup 之后谁接手
 */
void MarkdownPreviewController::requestMarkup(const QString &purpose)
{
    m_markupPurpose = purpose;
    QVariantMap args;
    args.insert(QStringLiteral("purpose"), purpose);
    enqueue(QStringLiteral("serialize"), args);
}

/**
 * @brief 请求把懒渲染的图表全部补上
 *
 * 导出与外部打开前必须先补渲染：`content-visibility` 与 IntersectionObserver
 * 让屏外图表停在占位状态，直接序列化会得到一堆 "Click to render"。
 */
void MarkdownPreviewController::requestFullRender()
{
    enqueue(QStringLiteral("renderAll"), QJsonObject());
}

/**
 * @brief 文档内搜索
 * @param text 关键词
 * @param forward 向后找
 * @param caseSensitive 区分大小写
 * @param restart 从头开始（关键词变了才为真）
 */
void MarkdownPreviewController::requestFind(const QString &text, bool forward,
                                            bool caseSensitive, bool restart)
{
    QVariantMap args;
    args.insert(QStringLiteral("text"), text);
    args.insert(QStringLiteral("forward"), forward);
    args.insert(QStringLiteral("caseSensitive"), caseSensitive);
    args.insert(QStringLiteral("restart"), restart);
    enqueue(QStringLiteral("find"), args);
}

/**
 * @brief 页面报回搜索结果
 * @param json {total, active}
 */
void MarkdownPreviewController::reportFindResult(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject object = doc.object();
    Q_EMIT findResult(object.value(QStringLiteral("total")).toInt(),
                      object.value(QStringLiteral("active")).toInt());
}

/**
 * @brief 跳到大纲里的某一块
 * @param anchor 页面 DOM 的 section id
 * @param blockIndex 块索引
 */
void MarkdownPreviewController::scrollToAnchor(const QString &anchor,
                                               int blockIndex)
{
    QVariantMap args;
    args.insert(QStringLiteral("anchor"), anchor);
    args.insert(QStringLiteral("blockIndex"), blockIndex);
    enqueue(QStringLiteral("scrollTo"), args);
}

/**
 * @brief 把一条命令放进队列（同 cmd 只留最新一条）
 * @param cmd 命令名
 * @param args 参数
 */
void MarkdownPreviewController::enqueue(const QString &cmd,
                                        const QVariantMap &args)
{
    enqueue(cmd, QJsonObject::fromVariantMap(args));
}

/**
 * @brief 把一条命令放进队列（对象形态的参数）
 * @param cmd 命令名
 * @param args 参数
 *
 * 「同 cmd 覆盖」是本类唯一的路径保证：连续敲键时队列里留最后一次；
 * setDocument 是全量重建，它一进队就把还没发出的 updateBlocks 清掉，
 * 否则两种写法会在页面上打架（切会话正是靠这条把上一会话的增量清干净）。
 *
 * 这里**不**按 available() 提前返回：能力缺失时根本没有人调用本类（菜单项
 * 与窗口都不出现），而把队列测试绑在编译期开关上，等于让 WebEngine 关闭的
 * 构建里整套命令队列逻辑没有测试可跑。
 */
void MarkdownPreviewController::enqueue(const QString &cmd,
                                        const QJsonObject &args)
{
    // 名字必须在协议表里：命令是字符串协议，写错时页面只会安静地什么都不做，
    // 所以在这里挡一道并留下日志，而不是让它一路漂到「功能不生效」。
    if (!MarkdownRuntime::commandNames().contains(cmd)) {
        qWarning() << "markdown: dropped unknown command:" << cmd;
        return;
    }
    if (cmd == QStringLiteral("setDocument")) {
        for (int i = m_queue.size() - 1; i >= 0; --i) {
            if (m_queue.at(i).first == QStringLiteral("updateBlocks")) {
                m_queue.removeAt(i);
            }
        }
    }
    const QString json =
        MarkdownRuntime::commandJson(cmd, ++m_seq, args);
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).first == cmd) {
            m_queue[i].second = json;
            Q_EMIT commandQueued();
            return;
        }
    }
    m_queue.append(qMakePair(cmd, json));
    Q_EMIT commandQueued();
}

/**
 * @brief 视图设置落盘
 * @param mode 三态模式（作为新会话的默认值写进 settings）
 *
 * 写内存里的 Settings 再 save()：与界面其它设置项同一套语义。这里每次都
 * save——预览窗口是低频操作，省不下什么，而「调完字号重启就没了」是
 * 实打实的抱怨。
 */
void MarkdownPreviewController::persistView(const QString &mode)
{
    if (!m_settings) {
        return;
    }
    m_settings->setMarkdownMode(mode);
    m_settings->setMarkdownFontSizePx(m_fontSizePx);
    m_settings->setMarkdownContentWidthPx(m_contentWidthPx);
    m_settings->setMarkdownShowOutline(m_showOutline);
    m_settings->setMarkdownAutoOpenWorkbench(m_autoOpenWorkbench);
    m_settings->save();
}

/**
 * @brief 从设置恢复视图偏好（新会话的三态默认值 + 全局偏好 + 自动开窗）
 */
void MarkdownPreviewController::loadView()
{
    if (!m_settings) {
        return;
    }
    const core::MarkdownSettings &markdown = m_settings->markdownOptions();
    m_defaultMode = markdown.mode;
    m_fontSizePx = markdown.fontSizePx;
    m_contentWidthPx = markdown.contentWidthPx;
    m_showOutline = markdown.showOutline;
    m_autoOpenWorkbench = markdown.autoOpenWorkbench;
}

} // namespace awb::markdown
