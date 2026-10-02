#ifndef AWB_MARKDOWN_MARKDOWNPREVIEWCONTROLLER_H
#define AWB_MARKDOWN_MARKDOWNPREVIEWCONTROLLER_H

#include "markdown/MarkdownChunker.h"
#include "markdown/MarkdownDocument.h"
#include "core/OpResult.h"

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::theme {
class Theme;
} // namespace awb::theme

namespace awb::markdown {

/// QML 单例 `Markdown`（AgentWorkbench.App 上的大写类型名）：预览窗口的全部
/// 状态与动作，也是页面 JS 与 C++ 之间那唯一一条命令队列的持有者。
///
/// 分工线在这里划开：本类不认识 WebEngine，也不认识 QML 类型——它只负责
/// 「文档 + 块 + 视图设置 + 待发送命令」，把命令送进视图、把视图拉取的资源
/// 供出去，都是 src/markdown/webengine/ 适配器的活。这样纯层能在没有
/// WebEngine 的构建里编译、在 tst_markdown 里没有浏览器地测试。
///
/// 命令队列而不是信号直发：QML 侧的 runJavaScript 只有 WebEngineView 这一个
/// 入口，视图还没加载完时发的命令会全部丢失。所以命令先进队列，适配器在
/// 「视图就绪」和「有新命令」两个时刻各排空一次，队列里只保留同一类的最新
/// 一条（连续五次编辑只会送出最后一次 setDocument/updateBlocks）。
class MarkdownPreviewController : public QObject
{
    Q_OBJECT

    /// 本构建是否具备预览能力；不具备时窗口与菜单项都不出现
    Q_PROPERTY(bool available READ available NOTIFY capabilitiesChanged)
    /// 不具备时的原因（面向用户）
    Q_PROPERTY(QString unavailableReason READ unavailableReason
                   NOTIFY capabilitiesChanged)
    /// 渲染栈版本串，关于信息与日志用
    Q_PROPERTY(QString rendererVersion READ rendererVersion
                   NOTIFY capabilitiesChanged)
    /// 窗口是否已经打开
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(QString fileTitle READ fileTitle NOTIFY documentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(QString readOnlyReason READ readOnlyReason NOTIFY readOnlyChanged)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY readOnlyChanged)
    /// 能否用「编辑 | 分栏 | 源码」三态（受 canEdit 约束）
    Q_PROPERTY(bool splitAvailable READ splitAvailable NOTIFY readOnlyChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY viewChanged)
    Q_PROPERTY(int fontSizePx READ fontSizePx WRITE setFontSizePx
                   NOTIFY viewChanged)
    Q_PROPERTY(int contentWidthPx READ contentWidthPx WRITE setContentWidthPx
                   NOTIFY viewChanged)
    Q_PROPERTY(bool showOutline READ showOutline WRITE setShowOutline
                   NOTIFY viewChanged)
    Q_PROPERTY(QVariantList outline READ outline NOTIFY outlineChanged)
    /// 文档字节数（状态栏显示）
    Q_PROPERTY(qint64 byteSize READ byteSize NOTIFY documentChanged)
    /// 外部改动的提示：窗口 QML 据此弹「重载 / 保留我的改动」
    Q_PROPERTY(bool changedOnDisk READ changedOnDisk
                   NOTIFY changedOnDiskChanged)

public:
    explicit MarkdownPreviewController(core::Settings *settings,
                                       theme::Theme *theme,
                                       QObject *parent = nullptr);

    bool available() const;
    QString unavailableReason() const;
    QString rendererVersion() const;

    /// 预览表面（awbmd:// 的 scheme handler 与它的 profile）装好了没有，
    /// 由 src/markdown/webengine/ 的 provider 在构造时置位。这里判的不是
    /// 「这个二进制链接了 WebEngine」而是「页面真的能被供出去」：scheme
    /// 注册顺序错了的时候前者照样为真，而页面的 fetch 全废——正文在、公式
    /// 与图表出不来。只给适配器调用——QML 侧一律读 available 属性。
    void setSurfaceReady(bool ready);
    bool active() const { return m_windowOpen; }
    QString filePath() const { return m_document.path(); }
    QString fileTitle() const { return m_document.fileName(); }
    bool dirty() const { return m_document.dirty(); }
    bool readOnly() const { return m_document.readOnly(); }
    QString readOnlyReason() const { return m_document.readOnlyReason(); }
    bool canEdit() const { return m_document.canEdit(); }
    bool splitAvailable() const { return m_document.canEdit(); }
    bool changedOnDisk() const { return m_changedOnDisk; }
    qint64 byteSize() const { return m_document.byteSize(); }

    // 视图设置的四组读写（WRITE 入口即 Q_INVOKABLE 的等价物）
    QString mode() const { return m_mode; }
    int fontSizePx() const { return m_fontSizePx; }
    int contentWidthPx() const { return m_contentWidthPx; }
    bool showOutline() const { return m_showOutline; }
    QVariantList outline() const { return m_outline; }

    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void setFontSizePx(int pixels);
    Q_INVOKABLE void setContentWidthPx(int pixels);
    Q_INVOKABLE void setShowOutline(bool show);

    /// 打开一份 Markdown：校验、读盘、切块，成功后发 windowRequested()。
    /// 失败只返回原因，不动既有文档，也不开窗。
    Q_INVOKABLE awb::core::OpResult openFile(const QString &path);

    // 窗口 QML 的生命周期播报
    Q_INVOKABLE void notifyWindowOpened();
    Q_INVOKABLE void notifyWindowClosed();

    // 编辑器回写与存盘
    Q_INVOKABLE void setEditorText(const QString &text);
    Q_INVOKABLE QString editorText() const { return m_document.text(); }
    Q_INVOKABLE awb::core::OpResult saveNow();
    // 磁盘那份重新读进来（放弃本地改动）
    Q_INVOKABLE awb::core::OpResult reloadFromDisk();
    // 差异提示消失（用户选了「保留我的改动」）
    Q_INVOKABLE void dismissDiskNotice();
    /// 重新比对磁盘形态并刷新 changedOnDisk。文件监视器与窗口激活各调一次：
    /// QFileSystemWatcher 在 Windows 上会静默丢掉「删除后重建」的文件，光靠它
    /// 会有「别人改了却始终不提示」的漏报，这条轮询式复查是它的兜底。
    Q_INVOKABLE void refreshDiskState();

    /// 页面 fetch('awbmd://runtime/doc.json') 的响应体。
    /// 每次调用都按当前状态重新生成，所以 rev 与内容不会错拍。
    Q_INVOKABLE QString documentBodyJson() const;

    /// 适配器排空命令队列用：取出队首命令 JSON，空串表示没有。
    Q_INVOKABLE QString takeCommand();
    /// 队列里是否还有未送出的命令
    Q_INVOKABLE bool hasPendingCommand() const { return !m_queue.isEmpty(); }

    // 页面报告（信标太小放不下的一律经 runJavaScript 返回值取回后交这里）
    Q_INVOKABLE void reportReady(const QString &json);
    Q_INVOKABLE void reportFrame(const QString &json);
    Q_INVOKABLE void reportError(const QString &json);
    Q_INVOKABLE void reportMarkup(const QString &html);

    /// 请求把当前渲染结果整篇序列化（导出 / 外部打开），结果从 reportMarkup
    /// 回来。purpose 取 "export" 或 "browser"，决定接手方。
    Q_INVOKABLE void requestMarkup(const QString &purpose);
    // 请求整篇渲染（把懒渲染的图表全部补上），只在导出/外部打开前需要
    Q_INVOKABLE void requestFullRender();
    // 文档内搜索：命令进队列，命中数从 reportFindResult 回来
    Q_INVOKABLE void requestFind(const QString &text, bool forward,
                                bool caseSensitive, bool restart);
    Q_INVOKABLE void reportFindResult(const QString &json);
    // 跳到大纲项
    Q_INVOKABLE void scrollToAnchor(const QString &anchor, int blockIndex);

    // 供适配器解析 awbmd://runtime/asset/<docId>/… 的两个取值
    QString docId() const { return m_docId; }
    QString assetRoot() const { return m_document.directory(); }

    // 块序列（测试与适配器都按它工作）
    const QVector<MarkdownBlock> &blocks() const { return m_blocks; }

Q_SIGNALS:
    /// 请求打开预览窗口：MainWindow.qml 里那个 Loader 的唯一激活条件
    void windowRequested();
    /// 有新命令排队，适配器应当来 takeCommand()
    void commandQueued();
    void activeChanged();
    void documentChanged();
    void dirtyChanged();
    void readOnlyChanged();
    void viewChanged();
    void outlineChanged();
    void capabilitiesChanged();
    void changedOnDiskChanged();
    /// 导出/外部打开的 HTML 已就绪（异步：页面要先补渲染再序列化）
    void markupReady(const QString &html, const QString &purpose);
    /// 页面报回来的渲染失败
    void renderFailed(const QString &message);
    /// 搜索结果（total/active）
    void findResult(int total, int active);
    /// 保存成功，窗口标题与吐司都要跟进
    void saved();

private:
    // 把一条命令放进队列（同类只留最新）
    void enqueue(const QString &cmd, const QVariantMap &args);
    void enqueue(const QString &cmd, const QJsonObject &args);
    // 设置变更后持久化
    void persistView();
    void loadView();

    core::Settings *m_settings;    ///< 视图设置的落点（不拥有）
    theme::Theme *m_theme;         ///< CSS 变量的来源（不拥有）
    MarkdownDocument m_document;   ///< 当前文档
    QVector<MarkdownBlock> m_blocks; ///< 当前块序列
    QVariantList m_outline;        ///< 由块序列派生的大纲
    QString m_docId;               ///< 本次打开的资源寻址 id
    int m_docSeq = 0;              ///< docId 的自增序号
    int m_rev = 0;                 ///< 文档版本，进 doc.json 的 query
    int m_seq = 0;                 ///< 命令序号，只用于日志对齐
    bool m_surfaceReady = false;   ///< 适配器是否已装好预览表面（见 setSurfaceReady）
    bool m_windowOpen = false;     ///< 窗口是否活着
    bool m_changedOnDisk = false;  ///< 外部改动提示的当前状态
    QString m_markupPurpose;       ///< 在飞的序列化请求用途
    QList<QPair<QString, QString>>
        m_queue;                   ///< 待发送命令：cmd → 命令 JSON

    QString m_mode;                ///< preview | split | source
    int m_fontSizePx = 0;          ///< 0 = 用主题基准
    int m_contentWidthPx = 0;      ///< 0 = 用主题基准
    bool m_showOutline = true;     ///< 大纲侧栏开关

    mutable int m_cachedRev = -1;  ///< documentBodyJson 的缓存版本
    mutable QString m_cachedBody;  ///< documentBodyJson 的缓存内容
};

} // namespace awb::markdown

#endif // AWB_MARKDOWN_MARKDOWNPREVIEWCONTROLLER_H
