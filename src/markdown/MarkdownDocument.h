#ifndef AWB_MARKDOWN_MARKDOWNDOCUMENT_H
#define AWB_MARKDOWN_MARKDOWNDOCUMENT_H

#include "core/OpResult.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>

class QFileSystemWatcher;

namespace awb::markdown {

/// 预览窗口里那一个磁盘 Markdown 文件的状态：文本、脏标记、只读判定，
/// 以及「载入时看到的字节形态」（BOM、行尾、是否合法 UTF-8）。
///
/// 存在的理由是**存盘不能改写文件**：用户从文件树右键打开一份 CRLF + BOM 的
/// README，Ctrl+S 之后应当还是 CRLF + BOM，而不是被"规范化"成 LF。所以载入
/// 时把形态记下来，保存时按原样写回；只有全新文件才用 LF、无 BOM。
///
/// 大小上限是产品要求（几百 KB ~ 2 MB 流畅，更大文件不该进来编辑）：超过
/// kOpenCapBytes 直接拒绝打开，超过 kEditCapBytes 允许预览但禁用编辑——
/// 一个改不动的预览窗口仍然有用，而一个每敲一键就重排 8 MB 文本的编辑器
/// 只会把整个应用拖死。只读的第三个来源是编码：不是合法 UTF-8 的文件解码
/// 已经不可逆了，再写回去就是把用户的数据换成一堆 U+FFFD，所以一律只读。
///
/// 外部改动经 QFileSystemWatcher 上报（自己保存引起的那一次不算：保存后
/// 记录磁盘快照，watcher 回调比对不出差异就闭嘴）。窗口 QML 决定怎么问用户。
class MarkdownDocument : public QObject
{
    Q_OBJECT

public:
    /// 允许打开的字节上限；超过它连预览都不给（首屏会卡住整个 WebView）。
    static constexpr qint64 kOpenCapBytes = 20LL * 1024 * 1024;
    /// 允许编辑的字节上限；超过它只读预览。
    static constexpr qint64 kEditCapBytes = 2LL * 1024 * 1024;

    explicit MarkdownDocument(QObject *parent = nullptr);
    ~MarkdownDocument() override;

    // 读盘并建立形态快照。失败（不存在、过大、无权限）返回 failure，
    // 此时文档内容保持原样不动。
    core::OpResult load(const QString &path);

    // 把当前文本按原形态写回 filePath()；成功即清除脏标记。
    core::OpResult save();

    // 关掉监视并清空全部状态（窗口关闭时调用）
    void clear();

    // 以「虚拟文档」承载一份没有磁盘文件的文本（提示词草稿的预览会话）：
    // 可编辑、可渲染、字节数照算（编码上限同样生效），save() 一律拒绝
    // （本就没有落盘目标，草稿的持久化走 tools 域），不监视磁盘、永远不脏。
    // title 只作显示名（fileName()）。
    void setVirtual(const QString &title, const QString &text);
    // 当前是否为虚拟文档
    bool isVirtual() const { return m_virtual; }

    // 是否已载入一份文件
    bool hasPath() const { return !m_path.isEmpty(); }
    QString path() const { return m_path; }
    // 显示名（不含目录）
    QString fileName() const;
    // 所在目录：文档里的相对图片路径以此为基准解析
    QString directory() const;

    QString text() const { return m_text; }
    // 编辑器回写。内容真的变了才置脏，改回原样即不再脏。
    void setText(const QString &text);

    bool dirty() const { return m_text != m_savedText; }
    // 只读 = 没有路径 / 文件不可写 / 太大 / 编码不可逆
    bool readOnly() const;
    // 只读原因（面向用户的英文源串；readOnly() 为假时为空）
    QString readOnlyReason() const;
    // 文本能否进编辑器（比 readOnly() 更严：无路径也不能编辑，存不回盘）
    bool canEdit() const;

    // 字节数（按当前形态序列化后的长度，不是字符数）
    qint64 byteSize() const;

    // 载入/保存时看到的磁盘形态是否与实际文件一致
    bool diskSnapshotMatches() const;
    // 主动复查一次外部改动（watcher 不可用时的兜底；也供测试直接驱动）
    bool externallyChanged() const;

    /// 把磁盘当前形态认作基线：内容仍然脏（我的改动没存盘），但不再报
    /// 「外部改动」。用户在「重载 / 保留我的改动」里选后者时调它——否则下一次
    /// 复查会把同一次外部改动再报一遍，提示永远关不掉。
    void acceptDiskState();

Q_SIGNALS:
    /// 文本内容变化（编辑、载入、重载）
    void contentChanged();
    /// 脏标记翻转
    void dirtyChanged();
    /// 磁盘上被别人改了（自己保存不触发）
    void externallyChangedByOther();
    /// 只读判定变化（大小、权限、编码任一因素翻转）
    void readOnlyChanged();

private:
    // 记下磁盘快照并开始监视 filePath()
    void trackFile();

    // 把 text 编回字节：BOM、行尾按载入时的形态
    QByteArray encodeText(const QString &text) const;

    QString m_path;                 ///< 绝对路径；空串表示无文档
    QString m_text;                 ///< 当前文本（不含行尾形态信息）
    QString m_savedText;            ///< 上次与磁盘一致的文本，脏标记的基准
    bool m_virtual = false;         ///< 虚拟文档（草稿会话）：无磁盘文件
    QString m_virtualTitle;         ///< 虚拟文档的显示名（fileName() 用）
    qint64 m_textBytes = 0;         ///< encodeText(m_text) 的字节长度（缓存）
    QByteArray m_bom;               ///< 载入时看到的 BOM，保存时原样写回
    bool m_crlf = false;            ///< 载入文件的行尾形态（全部为 CRLF 才认定）
    bool m_validUtf8 = true;        ///< 磁盘字节是否为合法 UTF-8
    QDateTime m_diskModified;       ///< 上次读/写后磁盘上的修改时间
    qint64 m_diskSize = -1;         ///< 上次读/写后磁盘上的字节数
    QFileSystemWatcher *m_watcher;  ///< 本文件的监视器（不拥有→由父对象管）
};

} // namespace awb::markdown

#endif // AWB_MARKDOWN_MARKDOWNDOCUMENT_H
