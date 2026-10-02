#include "awbtest.h"

#include "markdown/MarkdownDocument.h"

#include <QByteArray>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::OpResult;
using awb::markdown::MarkdownDocument;

namespace {

// 匿名 namespace 里的读写助手：测试要能绕开 MarkdownDocument 写盘，
// 才能造出「磁盘上是 BOM+CRLF」「磁盘被外部改过」这类来路。
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(path));
    file.write(bytes);
    file.close();
}

// 助手里的失败不做断言：QVERIFY 展开成 return，用在有返回值的函数里编译不过。
// 打不开就回空字节，让调用处的 QCOMPARE 报出真正要比对的内容。
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return bytes;
}

const char kBom[] = "\xEF\xBB\xBF";

} // namespace

/**
 * @brief MarkdownDocument 的测试类：读盘判定、字节级存盘回写、只读与脏标记
 */
class TestMarkdownDocument : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
    }
    void cleanupTestCase() { m_dir.remove(); }

    void testLoadMissingFile();
    void testLoadNormalisesLineEndings();
    void testSaveKeepsByteFidelity();
    void testSaveMixedLineEndingsStaysVerbatim();
    void testSaveWithoutPathFails();
    void testDirtyTracksSavedText();
    void testInvalidUtf8BecomesReadOnly();
    void testNulBytesBecomesReadOnly();
    void testWriteProtectedFileIsReadOnly();
    void testOpenCapRejectsBeforeReading();
    void testEditCapAllowsPreview();
    void testExternalChangeDetection();
    void testClearResetsEverything();

private:
    QString filePath(const QString &name) const
    {
        return m_dir.filePath(name);
    }

    QTemporaryDir m_dir;   ///< 全部读写都落在这里，绝不碰真实数据目录
};

AWB_TEST(TestMarkdownDocument)

/**
 * @brief 不存在、是目录、以及超过 20 MB 的读取都在打开之前被拒
 *
 * 超限那条必须在读之前判：为了拒绝一份 20 MB 的文件先把 20 MB 读进内存，
 * 等于把卡顿算在拒绝的路上。
 */
void TestMarkdownDocument::testLoadMissingFile()
{
    MarkdownDocument doc;
    const OpResult missing = doc.load(filePath(QStringLiteral("nope.md")));
    QVERIFY(!missing.ok);
    QVERIFY2(!missing.error.isEmpty(), "a rejection must explain itself");
    // 失败路径不动既有状态：没有文档时文本仍为空
    QVERIFY(doc.text().isEmpty());
    QVERIFY(doc.path().isEmpty());

    const OpResult isDir = doc.load(m_dir.path());
    QVERIFY(!isDir.ok);

    const QString big = filePath(QStringLiteral("big.md"));
    QFile file(big);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(MarkdownDocument::kOpenCapBytes + 1));
    file.close();
    const OpResult tooBig = doc.load(big);
    QVERIFY(!tooBig.ok);
    QVERIFY2(tooBig.error.contains(QStringLiteral("20 MB")),
             qPrintable(tooBig.error));
}

/**
 * @brief CRLF 文件在内存里是 \n，行尾形态另记一笔
 */
void TestMarkdownDocument::testLoadNormalisesLineEndings()
{
    const QString path = filePath(QStringLiteral("crlf.md"));
    writeFile(path, QByteArray("a\r\nb\r\n"));

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    QCOMPARE(doc.text(), QStringLiteral("a\nb\n"));
    QCOMPARE(doc.fileName(), QStringLiteral("crlf.md"));
    QCOMPARE(doc.directory(), QFileInfo(path).absolutePath());
    QCOMPARE(doc.byteSize(), qint64(6));
    QVERIFY(!doc.dirty());
    QVERIFY(!doc.readOnly());
    QVERIFY(doc.canEdit());
}

/**
 * @brief BOM + CRLF + 无尾换行的文件，改一处再存盘，其余字节逐字节不变
 *
 * 这是本类存在的理由：预览窗口带编辑功能，存盘就不能顺手改行尾、丢 BOM
 * 或多塞一个换行——那是把一个仓库里的文件改成了另一个文件。
 */
void TestMarkdownDocument::testSaveKeepsByteFidelity()
{
    const QByteArray original =
        QByteArray(kBom) + "# Title\r\n\r\nbody line";
    const QString path = filePath(QStringLiteral("fidelity.md"));
    writeFile(path, original);

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    // 内存形态：没有 BOM、没有 \r
    QCOMPARE(doc.text(), QStringLiteral("# Title\n\nbody line"));

    doc.setText(QStringLiteral("# Title\n\nbody line edited"));
    QVERIFY(doc.dirty());
    QVERIFY(doc.save().ok);
    QVERIFY(!doc.dirty());

    const QByteArray written = readFile(path);
    QVERIFY2(written.startsWith(QByteArray(kBom)),
             "the UTF-8 BOM must survive a round trip");
    QByteArray expected(kBom);
    expected += "# Title\r\n\r\nbody line edited";
    QCOMPARE(written, expected);
    // 存盘后磁盘快照同步，不把自己当成外部改动
    QVERIFY(doc.diskSnapshotMatches());
    QVERIFY(!doc.externallyChanged());
}

/**
 * @brief 混用行尾的文件按原文回写，不会被整篇改成 CRLF
 *
 * 一个 LF 文件里躺着几行 CRLF 时，「主导行尾」这种判断会把整篇转成 CRLF。
 * 实现只在**全部**换行都是 CRLF 时才认定 CRLF 形态，其余情况 \r 原样留在
 * 文本里，回写与来路一致。
 */
void TestMarkdownDocument::testSaveMixedLineEndingsStaysVerbatim()
{
    const QByteArray original = QByteArray("a\nb\r\nc\n");
    const QString path = filePath(QStringLiteral("mixed.md"));
    writeFile(path, original);

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    QCOMPARE(doc.text(), QStringLiteral("a\nb\r\nc\n"));

    QVERIFY(doc.save().ok);
    QCOMPARE(readFile(path), original);
}

/**
 * @brief 没有路径就没有存盘目标
 */
void TestMarkdownDocument::testSaveWithoutPathFails()
{
    MarkdownDocument doc;
    const OpResult saved = doc.save();
    QVERIFY(!saved.ok);
    QVERIFY(!saved.error.isEmpty());
}

/**
 * @brief 脏标记来自「与上次存盘内容比对」，改回原样会自动干净
 */
void TestMarkdownDocument::testDirtyTracksSavedText()
{
    const QString path = filePath(QStringLiteral("dirty.md"));
    writeFile(path, QByteArray("hello\n"));

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    QVERIFY(!doc.dirty());

    bool dirtySignals = false;
    connect(&doc, &MarkdownDocument::dirtyChanged, &doc,
            [&dirtySignals]() { dirtySignals = true; });

    doc.setText(QStringLiteral("hello world"));
    QVERIFY(doc.dirty());
    doc.setText(QStringLiteral("hello\n"));
    QVERIFY(!doc.dirty());
    QVERIFY2(dirtySignals, "dirtyChanged must fire when the flag flips");

    // 写入相同文本不发信号（每个按键都发一遍会拖慢绑定）
    dirtySignals = false;
    doc.setText(doc.text());
    QVERIFY(!dirtySignals);
}

/**
 * @brief 非法 UTF-8 一律只读，且原因说得清
 *
 * 判定用「解码再编码必须逐字节相同」而不是解码器的错误状态：Qt 5 没有
 * QStringDecoder。真正含 U+FFFD 的合法文件往返相等，不会被误判成坏文件。
 */
void TestMarkdownDocument::testInvalidUtf8BecomesReadOnly()
{
    const QString path = filePath(QStringLiteral("broken.md"));
    // 0xC3 后跟一个非法续字节：fromUtf8 会产出 U+FFFD，编码回来不是原字节
    writeFile(path, QByteArray("ok\n") + QByteArray("\xC3\x28more\n"));

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);   // 预览照开
    QVERIFY(doc.readOnly());
    QVERIFY(!doc.canEdit());
    QVERIFY2(doc.readOnlyReason().contains(QStringLiteral("UTF-8")),
             qPrintable(doc.readOnlyReason()));

    // 合法的 U+FFFD 不能被当成坏文件：它是合法编码的字符
    const QString good = filePath(QStringLiteral("valid.md"));
    writeFile(good, QString::fromUtf8("already \uFFFD here\n").toUtf8());
    MarkdownDocument doc2;
    QVERIFY(doc2.load(good).ok);
    QVERIFY2(!doc2.readOnly(), "a legal U+FFFD must not force read-only");
}

/**
 * @brief 含 NUL 的文件不是文本，只读
 */
void TestMarkdownDocument::testNulBytesBecomesReadOnly()
{
    const QString path = filePath(QStringLiteral("nul.md"));
    writeFile(path, QByteArray("text\0more\n", 9));

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    QVERIFY(doc.readOnly());
    QVERIFY(!doc.canEdit());
}

/**
 * @brief 只有读权限的文件只读，原因写的是「写保护」
 */
void TestMarkdownDocument::testWriteProtectedFileIsReadOnly()
{
    const QString path = filePath(QStringLiteral("locked.md"));
    writeFile(path, QByteArray("protected\n"));
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    QVERIFY2(doc.readOnly(), "a write-protected file must open read-only");
    QVERIFY2(doc.readOnlyReason().contains(QStringLiteral("write-protected")),
             qPrintable(doc.readOnlyReason()));
    QVERIFY(!doc.save().ok);

    // 恢复权限后可写：只读判定跟着实际状态走
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const OpResult saved = doc.save();
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY(!doc.readOnly());
}

/**
 * @brief 打开上限是「读之前」判的，且卡在边界上的文件仍然能进
 *
 * 两件事各自都要钉住：超限文件必须凭 QFileInfo 的大小直接拒绝（读进 20 MB
 * 再拒绝，等于把用户等的时长算在拒绝的路上）；而正好 20 MiB 的文件属于
 * 「允许预览」的一侧，边界判错会把能用的大文档挡在门外。
 *
 * 用 resize() 造大文件：NTFS 上是稀疏扩展，落盘几乎不花时间，读进来才花时间
 * ——而这正是本用例想量出来的那一次读到底有没有发生。
 */
void TestMarkdownDocument::testOpenCapRejectsBeforeReading()
{
    const QString kept = filePath(QStringLiteral("kept.md"));
    writeFile(kept, QByteArray("previous document\n"));

    MarkdownDocument doc;
    QVERIFY(doc.load(kept).ok);

    const QString big = filePath(QStringLiteral("over.md"));
    QFile over(big);
    QVERIFY(over.open(QIODevice::WriteOnly));
    QVERIFY(over.resize(MarkdownDocument::kOpenCapBytes + 1));
    over.close();

    const OpResult tooBig = doc.load(big);
    QVERIFY(!tooBig.ok);
    QVERIFY2(tooBig.error.contains(QStringLiteral("20 MB")), qPrintable(tooBig.error));
    // 失败后仍是上一份文档：预览窗口不该因为一次被拒的打开而变成空白
    QCOMPARE(doc.path(), QFileInfo(kept).absoluteFilePath());
    QCOMPARE(doc.text(), QStringLiteral("previous document\n"));

    const QString edge = filePath(QStringLiteral("edge.md"));
    QFile at(edge);
    QVERIFY(at.open(QIODevice::WriteOnly));
    QVERIFY(at.resize(MarkdownDocument::kOpenCapBytes));
    at.close();
    const OpResult loaded = doc.load(edge);
    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(doc.byteSize(), MarkdownDocument::kOpenCapBytes);
}

/**
 * @brief 超过 2 MB 编辑上限：能预览，不能编辑
 *
 * 上限是「编辑器会卡」而不是「文件有毒」，所以 readOnly 为真、canEdit 为假，
 * 而预览与存盘之外的功能一律照常。
 */
void TestMarkdownDocument::testEditCapAllowsPreview()
{
    const QString path = filePath(QStringLiteral("large.md"));
    QByteArray bytes(MarkdownDocument::kEditCapBytes + 16, 'a');
    bytes.append('\n');
    writeFile(path, bytes);

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    QVERIFY(doc.readOnly());
    QVERIFY(!doc.canEdit());
    QVERIFY2(doc.readOnlyReason().contains(QStringLiteral("2 MB")),
             qPrintable(doc.readOnlyReason()));
    QVERIFY(doc.byteSize() > MarkdownDocument::kEditCapBytes);
}

/**
 * @brief 磁盘那份变了要能被认出来，重载后恢复
 *
 * 监视器触发的 externallyChangedByOther 信号要等事件循环，测的是它背后的
 * 那次比对：diskSnapshotMatches() 与 externallyChanged()。
 */
void TestMarkdownDocument::testExternalChangeDetection()
{
    const QString path = filePath(QStringLiteral("watched.md"));
    writeFile(path, QByteArray("first\n"));

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    QVERIFY(!doc.externallyChanged());

    writeFile(path, QByteArray("second and longer\n"));
    QVERIFY(doc.externallyChanged());
    QVERIFY(!doc.diskSnapshotMatches());

    // 重新读入之后差异消失
    QVERIFY(doc.load(path).ok);
    QVERIFY(!doc.externallyChanged());

    // 文件被删掉：不是「一致」，界面据此提示而不是静默
    QFile::remove(path);
    QVERIFY(doc.externallyChanged());
}

/**
 * @brief clear() 之后回到「无文档」形态
 */
void TestMarkdownDocument::testClearResetsEverything()
{
    const QString path = filePath(QStringLiteral("closing.md"));
    writeFile(path, QByteArray("content\n"));

    MarkdownDocument doc;
    QVERIFY(doc.load(path).ok);
    doc.setText(QStringLiteral("content changed"));

    bool contentSignals = false;
    connect(&doc, &MarkdownDocument::contentChanged, &doc,
            [&contentSignals]() { contentSignals = true; });

    doc.clear();
    QVERIFY(doc.path().isEmpty());
    QVERIFY(doc.text().isEmpty());
    QVERIFY(!doc.hasPath());
    QCOMPARE(doc.byteSize(), qint64(0));
    QVERIFY(doc.readOnlyReason().isEmpty());
    QVERIFY(doc.diskSnapshotMatches());   // 无文档时「与磁盘相符」成立
    QVERIFY2(contentSignals, "clear() must tell the bindings to empty out");

    // 清空后原文件仍可再开
    QVERIFY(doc.load(path).ok);
    QCOMPARE(doc.text(), QStringLiteral("content\n"));
}

#include "tst_markdowndocument.moc"
