#include "markdown/MarkdownPreviewController.h"

#include "core/JsonStore.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "markdown/MarkdownRuntime.h"
#include "theme/Theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace awb::markdown {

namespace {

/// 认作 Markdown 的后缀（小写）。判定放在这一层而不是 QML：菜单项的可用态、
/// openFile() 的拒绝、以及将来 agent 报告的批量预览都要同一个答案。
bool isMarkdownPath(const QString &path)
{
    static const QRegularExpression re(
        QStringLiteral("\\.(md|markdown|mdown|mdtxt|mdtext|text)\\z"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(path).hasMatch();
}

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
 * @brief 构造门面：接好主题与文档的两条通知线，并从设置里恢复视图偏好
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

    // 主题换色不能重建文档：只把新的 CSS 变量送进页面。
    if (m_theme) {
        connect(m_theme, &theme::Theme::changed, this, [this]() {
            QVariantMap args;
            args.insert(QStringLiteral("vars"),
                        MarkdownRuntime::cssVariables(m_theme));
            args.insert(QStringLiteral("variant"), m_theme->variant());
            enqueue(QStringLiteral("setTheme"), args);
        });
    }

    // 外部改动只记一个状态位，怎么问用户是窗口的事。监视器与轮询复查共用
    // 同一个入口，两条路的结论必须一致。
    connect(&m_document, &MarkdownDocument::externallyChangedByOther,
            this, &MarkdownPreviewController::refreshDiskState);
    connect(&m_document, &MarkdownDocument::dirtyChanged,
            this, &MarkdownPreviewController::dirtyChanged);
    connect(&m_document, &MarkdownDocument::readOnlyChanged,
            this, &MarkdownPreviewController::readOnlyChanged);
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

/**
 * @brief 打开一份 Markdown 文档
 * @param path 磁盘路径
 * @return 成功，或带原因的失败
 *
 * 失败一律不开窗、不清空既有文档：菜单点错了不该把用户正在看的那份弄没。
 */
core::OpResult MarkdownPreviewController::openFile(const QString &path)
{
    if (!available()) {
        return core::OpResult::failure(unavailableReason());
    }
    if (!isMarkdownPath(path)) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownPreviewController", "Only Markdown files can be previewed here."));
    }
    const core::OpResult loaded = m_document.load(path);
    if (!loaded) {
        return loaded;
    }

    m_docId = QStringLiteral("d%1").arg(++m_docSeq);
    m_blocks = MarkdownChunker::split(m_document.text());
    m_outline = MarkdownRuntime::outlineFromBlocks(m_blocks);
    ++m_rev;
    m_changedOnDisk = false;

    AWB_PERF << "markdown: opened" << m_document.fileName()
             << m_document.byteSize() << "bytes ->" << m_blocks.size()
             << "blocks";

    Q_EMIT documentChanged();
    Q_EMIT outlineChanged();
    Q_EMIT dirtyChanged();
    Q_EMIT changedOnDiskChanged();
    Q_EMIT windowRequested();
    return core::OpResult::success();
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
 * @brief 窗口关闭：清状态，磁盘状态与文档对象一起放下
 */
void MarkdownPreviewController::notifyWindowClosed()
{
    if (!m_windowOpen) {
        return;
    }
    m_windowOpen = false;
    m_queue.clear();
    m_document.clear();
    m_blocks.clear();
    m_outline.clear();
    m_changedOnDisk = false;
    ++m_rev;

    Q_EMIT activeChanged();
    Q_EMIT documentChanged();
    Q_EMIT outlineChanged();
    Q_EMIT dirtyChanged();
    Q_EMIT changedOnDiskChanged();
}

/**
 * @brief 编辑器回写正文
 * @param text 新的全文
 *
 * 走「重切块 + 差异」而不是整篇重建：一次编辑通常只碰一两个块，把整篇
 * 再发一遍等于让页面把 2 MB 重排一次。
 */
void MarkdownPreviewController::setEditorText(const QString &text)
{
    if (m_document.text() == text) {
        return;
    }
    m_document.setText(text);
    const QVector<MarkdownBlock> next = MarkdownChunker::split(text);
    const MarkdownChunker::Diff change = MarkdownChunker::diff(m_blocks, next);
    m_blocks = next;
    const QVariantList nextOutline =
        MarkdownRuntime::outlineFromBlocks(m_blocks);
    const bool outlineDiffers = (nextOutline != m_outline);
    m_outline = nextOutline;
    ++m_rev;

    Q_EMIT documentChanged();
    if (outlineDiffers) {
        Q_EMIT outlineChanged();
    }

    if (m_windowOpen && !change.isNull()) {
        QJsonObject args;
        args.insert(QStringLiteral("rev"), m_rev);
        args.insert(QStringLiteral("startIndex"), change.startIndex);
        args.insert(QStringLiteral("removeCount"), change.removeCount);
        args.insert(QStringLiteral("blocks"), blocksJson(change.inserted));
        enqueue(QStringLiteral("updateBlocks"), args);
    }
}

/**
 * @brief 存盘
 * @return 成功或失败原因
 */
core::OpResult MarkdownPreviewController::saveNow()
{
    const core::OpResult written = m_document.save();
    if (written) {
        Q_EMIT saved();
    }
    return written;
}

/**
 * @brief 放弃本地改动，重新读盘
 * @return 成功或失败原因
 */
core::OpResult MarkdownPreviewController::reloadFromDisk()
{
    if (m_document.path().isEmpty()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownPreviewController", "There is no file to reload."));
    }
    const core::OpResult loaded = m_document.load(m_document.path());
    if (!loaded) {
        return loaded;
    }
    m_blocks = MarkdownChunker::split(m_document.text());
    m_outline = MarkdownRuntime::outlineFromBlocks(m_blocks);
    ++m_rev;
    m_changedOnDisk = false;

    Q_EMIT documentChanged();
    Q_EMIT outlineChanged();
    Q_EMIT dirtyChanged();
    Q_EMIT changedOnDiskChanged();
    if (m_windowOpen) {
        QVariantMap args;
        args.insert(QStringLiteral("rev"), m_rev);
        enqueue(QStringLiteral("setDocument"), args);
    }
    return core::OpResult::success();
}

/**
 * @brief 用户选择保留自己的改动，提示消失
 *
 * 必须同时把磁盘那份认作基线：只清状态位的话，下一次 refreshDiskState()
 * 会算出同样的差异、把提示再弹一遍，用户关不掉它。
 */
void MarkdownPreviewController::dismissDiskNotice()
{
    if (!m_changedOnDisk) {
        return;
    }
    m_document.acceptDiskState();
    m_changedOnDisk = false;
    Q_EMIT changedOnDiskChanged();
}

/**
 * @brief 重新比对磁盘形态，刷新 changedOnDisk
 *
 * 无文档时差异一律为假：没有基线可比。
 */
void MarkdownPreviewController::refreshDiskState()
{
    const bool changed = m_document.hasPath() && m_document.externallyChanged();
    if (changed == m_changedOnDisk) {
        return;
    }
    m_changedOnDisk = changed;
    Q_EMIT changedOnDiskChanged();
}

/**
 * @brief 视图模式
 * @param mode preview / split / source；其它取值忽略
 */
void MarkdownPreviewController::setMode(const QString &mode)
{
    if (mode != QStringLiteral("preview") && mode != QStringLiteral("split")
        && mode != QStringLiteral("source")) {
        return;
    }
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    persistView();
    Q_EMIT viewChanged();
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
    persistView();
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
    persistView();
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
    persistView();
    Q_EMIT viewChanged();
    QVariantMap args;
    args.insert(QStringLiteral("showOutline"), show);
    enqueue(QStringLiteral("setView"), args);
}

/**
 * @brief 页面 fetch('doc.json') 的响应体
 * @return 整篇文档 + 块 + 视图 + 主题 + 文案的 JSON
 *
 * 按 rev 缓存：一次渲染周期里页面可能取多次（首屏、字体切换后的重取），
 * 每次重新序列化 2 MB 文本是白干。缓存只在 rev 变化时失效。
 */
QString MarkdownPreviewController::documentBodyJson() const
{
    if (m_cachedRev == m_rev && !m_cachedBody.isEmpty()) {
        return m_cachedBody;
    }

    QJsonObject view;
    view.insert(QStringLiteral("mode"), m_mode);
    view.insert(QStringLiteral("fontSizePx"), m_fontSizePx);
    view.insert(QStringLiteral("contentWidthPx"), m_contentWidthPx);
    view.insert(QStringLiteral("showOutline"), m_showOutline);
    view.insert(QStringLiteral("canEdit"), canEdit());

    QJsonObject root;
    root.insert(QStringLiteral("rev"), m_rev);
    root.insert(QStringLiteral("docId"), m_docId);
    root.insert(QStringLiteral("path"), m_document.path());
    root.insert(QStringLiteral("title"), m_document.fileName());
    root.insert(QStringLiteral("directory"), m_document.directory());
    root.insert(QStringLiteral("bytes"), double(m_document.byteSize()));
    root.insert(QStringLiteral("readOnly"), readOnly());
    root.insert(QStringLiteral("readOnlyReason"), m_document.readOnlyReason());
    root.insert(QStringLiteral("assetBase"),
                MarkdownRuntime::assetUrl(m_docId, QStringLiteral("")).toString());
    root.insert(QStringLiteral("view"), view);
    root.insert(QStringLiteral("vars"),
                QJsonObject::fromVariantMap(
                    MarkdownRuntime::cssVariables(m_theme)));
    root.insert(QStringLiteral("strings"), MarkdownRuntime::pageStrings());
    root.insert(QStringLiteral("blocks"), blocksJson(m_blocks));
    root.insert(QStringLiteral("outline"), QJsonArray::fromVariantList(m_outline));

    const QString body = QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    // const 方法要写缓存：这两支是页面侧的唯一真相，缓存错了就是渲染错了。
    m_cachedRev = m_rev;
    m_cachedBody = body;
    return body;
}

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
 * @param json 页面报回的能力与耗时（{workers,fetchApi,firstPaintMs}）
 *
 * 就绪即把视图与主题两条命令补发一次：doc.json 里已经带了它们，这条只是
 * 让「页面加载完成」与「设置命令到达」的顺序无关紧要。
 */
void MarkdownPreviewController::reportReady(const QString &json)
{
    AWB_PERF << "markdown: page ready" << json.left(200);
    QVariantMap args;
    args.insert(QStringLiteral("mode"), m_mode);
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
 * @param json {parseMs, mountMs, blockCount}
 */
void MarkdownPreviewController::reportFrame(const QString &json)
{
    AWB_PERF << "markdown: frame" << json.left(200);
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
 * @brief 页面把整篇序列化好的 HTML 交回来
 * @param html 自包含的 HTML 文本
 *
 * purpose 由 requestMarkup() 记住：一次只可能有一个序列化请求在飞（按钮
 * 点了会等结果回来才再可用），所以不需要把 id 传进传出。
 */
void MarkdownPreviewController::reportMarkup(const QString &html)
{
    Q_EMIT markupReady(html, m_markupPurpose);
    m_markupPurpose.clear();
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
 * 否则两种写法会在页面上打架。
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
 *
 * 写内存里的 Settings 再 save()：与界面其它设置项同一套语义（值一改就发
 * valueChanged，落盘由调用方决定）。这里每次都 save——预览窗口是低频操作，
 * 省不下什么，而「调完字号重启就没了」是实打实的抱怨。
 */
void MarkdownPreviewController::persistView()
{
    if (!m_settings) {
        return;
    }
    // 载荷里也有视图状态，改了不改 rev 也得让它重算：缓存只按 rev 失效的话，
    // 「改完字号后页面重新 fetch doc.json」会拿到旧的字号。
    m_cachedRev = -1;
    m_settings->setMarkdownMode(m_mode);
    m_settings->setMarkdownFontSizePx(m_fontSizePx);
    m_settings->setMarkdownContentWidthPx(m_contentWidthPx);
    m_settings->setMarkdownShowOutline(m_showOutline);
    m_settings->save();
}

/**
 * @brief 从设置恢复视图偏好
 */
void MarkdownPreviewController::loadView()
{
    if (!m_settings) {
        return;
    }
    const core::MarkdownSettings &markdown = m_settings->markdownOptions();
    m_mode = markdown.mode;
    m_fontSizePx = markdown.fontSizePx;
    m_contentWidthPx = markdown.contentWidthPx;
    m_showOutline = markdown.showOutline;
}

} // namespace awb::markdown
