#include "markdown/MarkdownDocument.h"

#include "core/JsonStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>

namespace awb::markdown {

namespace {

/// UTF-8 BOM 的三字节形态。
const char kUtf8Bom[] = "\xEF\xBB\xBF";

// 去掉 UTF-8 BOM，只留正文字节（BOM 本身由调用方按长度差取回）
QByteArray stripBom(const QByteArray &bytes)
{
    if (bytes.startsWith(QByteArray(kUtf8Bom, 3))) {
        return bytes.mid(3);
    }
    return bytes;
}

/// 字节是否为「可逆的」UTF-8：解码再编码回来必须逐字节相同。
///
/// 这里不用 QStringDecoder 的 state().hasError()：Qt 5 没有这个类，而本文件
/// 要在两条路线上都编译。fromUtf8 对非法序列一律换 U+FFFD，所以「往返不相等」
/// 恰好等价于「原字节里有非法序列」；真正含 U+FFFD 的合法文件往返相等，
/// 不会被误判。
bool isReversibleUtf8(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes).toUtf8() == bytes;
}

// 文本里是否出现 NUL：Markdown 不该有，出现了就说明这不是文本文件
bool hasNulByte(const QByteArray &bytes)
{
    return bytes.contains('\0');
}

// 面向用户的串一律写成 QCoreApplication::translate("<裸类名>", "字面量")：
// 包一层 source(text) 助手的话，lupdate 从函数形参里抽不到任何东西，整个模块
// 的提示就永远不进 .ts（不报错、测试也发现不了）。裸类名与 lupdate 记的上下文
// 一致，见 AGENTS.md 的 i18n 一节。

} // namespace

/**
 * @brief 构造空文档并建好文件监视器
 * @param parent 父对象
 */
MarkdownDocument::MarkdownDocument(QObject *parent)
    : QObject(parent),
      m_watcher(new QFileSystemWatcher(this))
{
    // watcher 的回调只做一件事：重新比对磁盘快照。差异成立才发信号，
    // 于是自己保存引起的那一次改动天然被过滤掉。
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        if (!m_path.isEmpty() && externallyChanged()) {
            Q_EMIT externallyChangedByOther();
        }
    });
}

/**
 * @brief 析构：默认实现（watcher 是子对象，随父析构）
 */
MarkdownDocument::~MarkdownDocument() = default;

/**
 * @brief 读入 path 并建立形态快照
 * @param path 绝对或相对的磁盘路径
 * @return 成功，或带用户可读原因的 failure
 *
 * 判定顺序是刻意的：先探存在性再读字节，过大文件不读内容（读 20 MB 只为
 * 拒绝它，等于把卡顿算在拒绝的路上）。失败路径一律不动既有内容，调用方
 * 因此可以放心地在失败后继续显示上一份文档。
 */
core::OpResult MarkdownDocument::load(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownDocument", "The file does not exist."));
    }
    if (info.size() > kOpenCapBytes) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownDocument", "The file is larger than the 20 MB preview limit."));
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownDocument", "The file could not be opened for reading."));
    }
    const QByteArray raw = file.readAll();
    file.close();

    const bool wasReadOnly = readOnly();

    const bool hasBom = raw.startsWith(QByteArray(kUtf8Bom, 3));
    m_bom = hasBom ? raw.left(3) : QByteArray();
    const QByteArray body = stripBom(raw);
    m_validUtf8 = !hasNulByte(body) && isReversibleUtf8(body);

    // 行尾：只把 \r\n 归一成 \n 存进内存，单独的 \r 当普通字符保留，
    // 这样编码回去与来路逐字节一致。
    //
    // CRLF 形态必须**全部**换行都是 CRLF 才认定：混用（一个 LF 文件里躺着
    // 一行 CRLF）时按 CRLF 保存会把整篇改成 CRLF，那是实打实的字节改动。
    // 判假时 \r 留在文本里原样回写，两种情况下 encodeText(text) 都与来路
    // 逐字节相同——这是「存盘不破坏文件」这条承诺的全部依据。
    QString text = QString::fromUtf8(body);
    m_crlf = body.contains("\r\n") && body.count('\n') == body.count('\r');
    if (m_crlf) {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }

    m_path = info.absoluteFilePath();
    m_text = text;
    m_savedText = text;
    m_textBytes = raw.size();

    trackFile();

    Q_EMIT contentChanged();
    Q_EMIT dirtyChanged();
    if (wasReadOnly != readOnly()) {
        Q_EMIT readOnlyChanged();
    }
    return core::OpResult::success();
}

/**
 * @brief 按载入时的形态写回磁盘
 * @return 成功，或带原因的 failure
 *
 * 只读判定在这里再查一次：从界面点亮保存按钮到用户真的按下之间，文件可能
 * 已经被别的程序改成 UTF-16，或者权限变了。
 */
core::OpResult MarkdownDocument::save()
{
    if (m_path.isEmpty()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "MarkdownDocument", "There is no file to save to."));
    }
    if (!canEdit()) {
        return core::OpResult::failure(readOnlyReason());
    }

    const QByteArray bytes = encodeText(m_text);
    const QFileInfo info(m_path);
    QDir().mkpath(info.absolutePath());
    const core::OpResult written = core::JsonStore::writeBytes(m_path, bytes);
    if (!written) {
        return written;
    }

    m_savedText = m_text;
    trackFile();
    Q_EMIT dirtyChanged();
    if (readOnly()) {
        Q_EMIT readOnlyChanged();
    }
    return core::OpResult::success();
}

/**
 * @brief 清空文档并停止监视
 */
void MarkdownDocument::clear()
{
    const bool wasReadOnly = readOnly();
    if (!m_path.isEmpty()) {
        m_watcher->removePath(m_path);
    }
    m_path.clear();
    m_text.clear();
    m_savedText.clear();
    m_textBytes = 0;
    m_bom.clear();
    m_crlf = false;
    m_validUtf8 = true;
    m_diskModified = QDateTime();
    m_diskSize = -1;

    Q_EMIT contentChanged();
    Q_EMIT dirtyChanged();
    if (wasReadOnly != readOnly()) {
        Q_EMIT readOnlyChanged();
    }
}

/**
 * @brief 显示名（不含目录）
 * @return basename，无文档时为空串
 */
QString MarkdownDocument::fileName() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

/**
 * @brief 所在目录，文档内的相对资源以此为基准
 * @return 绝对目录路径，无文档时为空串
 */
QString MarkdownDocument::directory() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).absolutePath();
}

/**
 * @brief 编辑器回写文本
 * @param text 新内容（行尾一律 \n）
 *
 * 脏标记是「与磁盘快照比对」而不是布尔标志，所以改回原样会自动不再脏，
 * 窗口标题里的 ● 无需界面侧记账。
 */
void MarkdownDocument::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    const bool wasDirty = dirty();
    const bool wasReadOnly = readOnly();
    const qint64 sizeBefore = m_textBytes;

    m_text = text;
    m_textBytes = encodeText(text).size();

    Q_EMIT contentChanged();
    if (wasDirty != dirty()) {
        Q_EMIT dirtyChanged();
    }
    // 只有跨过编辑上限才需要通知只读翻转，否则每个按键都会发一次信号。
    if (wasReadOnly != readOnly()
        || (sizeBefore <= kEditCapBytes && m_textBytes > kEditCapBytes)) {
        Q_EMIT readOnlyChanged();
    }
}

/**
 * @brief 当前是否只读
 * @return 只读为真
 */
bool MarkdownDocument::readOnly() const
{
    return !m_validUtf8
        || (!m_path.isEmpty() && byteSize() > kEditCapBytes)
        || (!m_path.isEmpty()
            && !QFileInfo(m_path).isWritable());
}

/**
 * @brief 只读的原因
 * @return 面向用户的英文源串；可编辑时为空串
 */
QString MarkdownDocument::readOnlyReason() const
{
    if (!m_validUtf8) {
        return QCoreApplication::translate(
            "MarkdownDocument",
            "The file is not valid UTF-8, so editing is disabled to keep it "
            "from being corrupted.");
    }
    if (!m_path.isEmpty() && byteSize() > kEditCapBytes) {
        return QCoreApplication::translate(
            "MarkdownDocument",
            "The file is larger than the 2 MB editing limit, so it is opened "
            "read-only.");
    }
    if (!m_path.isEmpty() && !QFileInfo(m_path).isWritable()) {
        return QCoreApplication::translate(
            "MarkdownDocument", "The file is write-protected.");
    }
    return QString();
}

/**
 * @brief 文本能否进编辑器
 * @return 能编辑为真
 *
 * 比 readOnly() 多一条：没有路径就存不回盘，编辑了也是白编辑——预览窗口
 * 只针对磁盘文件，所以无文档时编辑器同样不开。
 */
bool MarkdownDocument::canEdit() const
{
    return !m_path.isEmpty() && !readOnly();
}

/**
 * @brief 当前内容按原形态编码后的字节数
 * @return 字节长度
 *
 * 值是 load()/setText() 时算好的，不是每次现编：readOnly() 与界面绑定都要
 * 读它，而「编一遍 2 MB 只为拿长度」会让每个按键都付一次全篇编码的代价——
 * 这正是本功能要解决的性能问题本身。
 */
qint64 MarkdownDocument::byteSize() const
{
    return m_textBytes;
}

/**
 * @brief 磁盘上的文件是否仍与载入/保存时的快照一致
 * @return 一致为真
 */
bool MarkdownDocument::diskSnapshotMatches() const
{
    if (m_path.isEmpty()) {
        return true;
    }
    const QFileInfo info(m_path);
    if (!info.exists()) {
        return false;
    }
    return info.lastModified() == m_diskModified && info.size() == m_diskSize;
}

/**
 * @brief 主动复查外部改动
 * @return 磁盘内容与我们的快照不符时为真
 */
bool MarkdownDocument::externallyChanged() const
{
    if (m_path.isEmpty()) {
        return false;
    }
    return !diskSnapshotMatches();
}

/**
 * @brief 承认磁盘现状为新基线，但不动文本与脏标记
 */
void MarkdownDocument::acceptDiskState()
{
    if (m_path.isEmpty()) {
        return;
    }
    trackFile();
}

/**
 * @brief 重新监视 filePath() 并刷新磁盘快照
 *
 * QFileSystemWatcher 在文件被删除后会静默丢掉它（Windows 上尤其如此），
 * 所以每次读写都重新 addPath；已经监视着同一路径时 remove/add 一次即可，
 * 不必判断。
 */
void MarkdownDocument::trackFile()
{
    if (m_path.isEmpty()) {
        return;
    }
    m_watcher->removePath(m_path);
    m_watcher->addPath(m_path);

    const QFileInfo info(m_path);
    m_diskModified = info.lastModified();
    m_diskSize = info.exists() ? info.size() : -1;
}

/**
 * @brief 把文本编回磁盘字节
 * @param text 内存形态的文本（\n 行尾）
 * @return 带 BOM、按原行尾的字节
 *
 * CRLF 的替换必须在 UTF-8 编码之后按字节做还是先按字符做？这里先按字符
 * 替换再前置 BOM：BOM 在文件头，不参与行尾，两种顺序结果相同，而按字符
 * 替换少一次 QByteArray 上的全串扫描。
 */
QByteArray MarkdownDocument::encodeText(const QString &text) const
{
    QString out = text;
    if (m_crlf) {
        out.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    }
    QByteArray bytes = out.toUtf8();
    bytes.prepend(m_bom);
    return bytes;
}

} // namespace awb::markdown
