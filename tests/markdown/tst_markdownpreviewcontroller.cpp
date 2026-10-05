#include "awbtest.h"

#include "core/Paths.h"
#include "core/Settings.h"
#include "markdown/MarkdownPreviewController.h"
#include "markdown/MarkdownRuntime.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::MarkdownSettings;
using awb::core::OpResult;
using awb::core::Paths;
using awb::core::Settings;
using awb::markdown::MarkdownPreviewController;
using awb::markdown::MarkdownRuntime;

namespace {

/// 预览能力是编译期开关（Qt 6 + WebEngine）。关掉它的构建里这套 UI 根本不
/// 存在，本类仍然要编译通过——所以「需要文档已打开」的用例在没有能力时显式
/// 跳过，而不是让 OFF 构建的测试变红，或悄悄什么都不测。
#define AWB_SKIP_IF_NO_PREVIEW()                                     \
    if (!MarkdownRuntime::isSupportedBuild()) {                       \
        QSKIP("Markdown preview is compiled out in this build");      \
    }

QString writeDoc(const QString &dir, const QString &name, const QString &text)
{
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    // 助手返回路径，所以不能在里面用 QVERIFY（它展开成 return）：
    // 写不进去时回空串，由 openFile() 的失败断言把问题报出来。
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write(text.toUtf8());
    file.close();
    return path;
}

// 命令 JSON 的解析助手：测试按 cmd 与 args 断言，不比对整串
QJsonObject parseCommand(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject();
}

// 排空当前队列，返回收到的命令名（按出队顺序）
QStringList drain(MarkdownPreviewController &controller)
{
    QStringList cmds;
    for (QString json = controller.takeCommand(); !json.isEmpty();
         json = controller.takeCommand()) {
        cmds << parseCommand(json).value(QStringLiteral("cmd")).toString();
    }
    return cmds;
}

} // namespace

/**
 * @brief MarkdownPreviewController 的测试类：能力判定、文档打开校验、命令队列
 *        语义、doc.json 载荷、视图偏好持久化与磁盘差异提示
 */
class TestMarkdownPreviewController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(!m_dir.path().isEmpty());
        QVERIFY(m_dir.isValid());
        // 设置读写一律落在这个临时根里，绝不碰开发者的 settings.json
        Paths::setDataRootForTesting(m_dir.path());
    }
    void cleanupTestCase()
    {
        Paths::setDataRootForTesting(QString());
        m_dir.remove();
    }

    void init() { m_settings = new Settings(this); }
    void cleanup()
    {
        delete m_settings;
        m_settings = nullptr;
        // 用例之间必须互不知情：视图偏好会落进 settings.json，留着它下一个
        // 用例的初值就变了（同一棵树里跑两次顺序还不同）。
        QFile::remove(Settings::settingsFilePath());
    }

    void testAvailabilityGate();
    void testOpenFileValidatesExtension();
    void testOpenFileRejectsMissingAndHuge();
    void testOpenFilePopulatesStateAndRequestsWindow();
    void testOpenFileFailureKeepsPreviousDocument();
    void testDocumentBodyJsonShape();
    void testDocumentBodyJsonIsCachedPerRev();
    void testCommandQueueKeepsLatestPerCmd();
    void testCollapsedCommandCarriesLatestBlocks();
    void testSetDocumentDropsPendingUpdateBlocks();
    void testEditorEditEnqueuesDeltaOnly();
    void testViewSettersPersistAndEnqueue();
    void testModeRejectsUnknownValue();
    void testFindAndScrollCommands();
    void testReportsAndSignals();
    void testDiskNoticeAndReload();
    void testWindowCloseKeepsSessions();
    void testSessionsOpenSwitchCloseAndDraft();
    void testPageUrlAndPreviewEligibility();
    void testExportLandingWritesFiles();
    void testQmlCallableMethodsAreInvokable();

private:
    QTemporaryDir m_dir;            ///< 数据根与文档都在这棵树里
    Settings *m_settings = nullptr; ///< 每个用例新建，读的是临时根
};

AWB_TEST(TestMarkdownPreviewController)

/**
 * @brief 能力判定成对出现，且必须等适配器把表面装上
 */
void TestMarkdownPreviewController::testAvailabilityGate()
{
    MarkdownPreviewController controller(m_settings, nullptr);
    // 没有 scheme handler 就没有「能被供出去的页面」，此时菜单项与窗口都
    // 不许出现——否则注册顺序写错的那种缺陷会表现成一片 404。
    QVERIFY(!controller.available());
    QVERIFY2(!controller.unavailableReason().isEmpty(),
             "a build without the feature must say why");
    controller.setSurfaceReady(true);
    QCOMPARE(controller.available(), MarkdownRuntime::isSupportedBuild());
    QCOMPARE(controller.unavailableReason().isEmpty(),
             MarkdownRuntime::isSupportedBuild());
    if (MarkdownRuntime::isSupportedBuild()) {
        QVERIFY(controller.rendererVersion().contains(
            QStringLiteral("markdown-it")));
    }
    QVERIFY(!controller.active());
}

/**
 * @brief 不是 Markdown 的后缀一律拒开
 *
 * 判定在 C++ 而不是文件树的菜单里：右键菜单、将来的批量预览、以及任何直接
 * 调 openFile() 的路径，都必须得到同一个答案。
 */
void TestMarkdownPreviewController::testOpenFileValidatesExtension()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);

    const QString txt = writeDoc(m_dir.path(), QStringLiteral("notes.txt"),
                                 QStringLiteral("hello\n"));
    const OpResult rejected = controller.openFile(txt);
    QVERIFY(!rejected.ok);
    QVERIFY(!rejected.error.isEmpty());
    QVERIFY(controller.filePath().isEmpty());
    QVERIFY(!controller.active());

    // 六种后缀都认，且大小写不敏感
    const QStringList accepted = {
        QStringLiteral("a.md"),      QStringLiteral("b.markdown"),
        QStringLiteral("c.mdown"),   QStringLiteral("d.mdtxt"),
        QStringLiteral("e.mdtext"),  QStringLiteral("f.text"),
        QStringLiteral("G.MD"),
    };
    for (const QString &name : accepted) {
        const QString path =
            writeDoc(m_dir.path(), name, QStringLiteral("# t\n"));
        QVERIFY2(controller.openFile(path).ok, qPrintable(name));
    }
}

/**
 * @brief 不存在、是目录、以及超过 20 MB 的文件都拒开，原因来自文档层
 */
void TestMarkdownPreviewController::testOpenFileRejectsMissingAndHuge()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);

    QVERIFY(!controller.openFile(QDir(m_dir.path())
                                     .filePath(QStringLiteral("ghost.md"))).ok);
    QVERIFY(!controller.openFile(m_dir.path()).ok);

    const QString big = QDir(m_dir.path()).filePath(QStringLiteral("big.md"));
    QFile file(big);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(21LL * 1024 * 1024));
    file.close();
    const OpResult result = controller.openFile(big);
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("20 MB")),
             qPrintable(result.error));
}

/**
 * @brief 成功打开：状态齐备、开窗信号每次都发、块与大纲都算好
 */
void TestMarkdownPreviewController::testOpenFilePopulatesStateAndRequestsWindow()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);

    const QString body =
        QStringLiteral("# Alpha\n\nbody one\n\n## Beta\n\n- a\n- b\n");
    const QString path = writeDoc(m_dir.path(), QStringLiteral("ready.md"), body);

    int requests = 0;
    connect(&controller, &MarkdownPreviewController::windowRequested, &controller,
            [&requests]() { ++requests; });
    int documentChanges = 0;
    connect(&controller, &MarkdownPreviewController::documentChanged, &controller,
            [&documentChanges]() { ++documentChanges; });

    QVERIFY(controller.openFile(path).ok);
    QCOMPARE(requests, 1);
    QCOMPARE(documentChanges, 1);
    QCOMPARE(controller.filePath(), QFileInfo(path).absoluteFilePath());
    QCOMPARE(controller.fileTitle(), QStringLiteral("ready.md"));
    QCOMPARE(controller.byteSize(), qint64(body.toUtf8().size()));
    QVERIFY(!controller.dirty());
    QVERIFY(!controller.readOnly());
    QVERIFY(controller.canEdit());
    QVERIFY(controller.splitAvailable());
    QCOMPARE(controller.blocks().size(), 4);
    QCOMPARE(controller.outline().size(), 2);
    QCOMPARE(controller.outline().at(0).toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Alpha"));
    // 资源寻址 id 与文档目录一起给适配器（awbmd://runtime/asset/<id>/…）
    QVERIFY(!controller.docId().isEmpty());
    QCOMPARE(controller.assetRoot(), QFileInfo(path).absolutePath());

    // 再开一次同一份文件仍然请求开窗：把已关的窗口再叫起来是正常用法
    QVERIFY(controller.openFile(path).ok);
    QCOMPARE(requests, 2);
    // 换文档换 docId：旧文档的相对资源 URL 必须失效而不是命中缓存
    const QString other =
        writeDoc(m_dir.path(), QStringLiteral("other.md"), QStringLiteral("# o\n"));
    const QString firstDocId = controller.docId();
    QVERIFY(controller.openFile(other).ok);
    QVERIFY(controller.docId() != firstDocId);
}

/**
 * @brief 打开失败不能把用户正在看的那份弄没
 */
void TestMarkdownPreviewController::testOpenFileFailureKeepsPreviousDocument()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString first = writeDoc(m_dir.path(), QStringLiteral("first.md"),
                                   QStringLiteral("# one\n"));
    QVERIFY(controller.openFile(first).ok);

    QVERIFY(!controller.openFile(QDir(m_dir.path())
                                     .filePath(QStringLiteral("nope.md"))).ok);
    QCOMPARE(controller.filePath(), QFileInfo(first).absoluteFilePath());
    QCOMPARE(controller.fileTitle(), QStringLiteral("first.md"));
    QCOMPARE(controller.blocks().size(), 1);
}

/**
 * @brief doc.json 的字段就是页面与 C++ 的全部契约，逐字段钉住
 *
 * 页面只 fetch 这一个对象，少一个字段的表现是一段空白或一句没有文案的提示，
 * 所以连 view / vars / strings / outline 一起断言。
 */
void TestMarkdownPreviewController::testDocumentBodyJsonShape()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString path = writeDoc(m_dir.path(), QStringLiteral("payload.md"),
                                  QStringLiteral("# Title\n\nbody\n"));
    QVERIFY(controller.openFile(path).ok);

    const QJsonObject root = parseCommand(controller.documentBodyJson().toUtf8());
    QVERIFY(!root.isEmpty());
    QVERIFY(root.value(QStringLiteral("rev")).toInt() >= 1);
    QCOMPARE(root.value(QStringLiteral("path")).toString(),
             QFileInfo(path).absoluteFilePath());
    QCOMPARE(root.value(QStringLiteral("title")).toString(),
             QStringLiteral("payload.md"));
    QCOMPARE(root.value(QStringLiteral("readOnly")).toBool(), false);
    QVERIFY(root.value(QStringLiteral("assetBase")).toString()
                .contains(QStringLiteral("/asset/")));
    QCOMPARE(root.value(QStringLiteral("docId")).toString(), controller.docId());

    const QJsonObject view = root.value(QStringLiteral("view")).toObject();
    QCOMPARE(view.value(QStringLiteral("mode")).toString(), controller.mode());
    QVERIFY(view.contains(QStringLiteral("fontSizePx")));
    QVERIFY(view.contains(QStringLiteral("contentWidthPx")));
    QVERIFY(view.contains(QStringLiteral("showOutline")));
    QVERIFY(view.value(QStringLiteral("canEdit")).toBool());

    QVERIFY(!root.value(QStringLiteral("strings")).toObject().isEmpty());
    QVERIFY(!root.value(QStringLiteral("vars")).toObject().isEmpty());

    const QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
    QCOMPARE(blocks.size(), 2);
    const QJsonObject first = blocks.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("i")).toInt(), 0);
    QCOMPARE(first.value(QStringLiteral("s")).toInt(), 0);
    QCOMPARE(first.value(QStringLiteral("e")).toInt(), 0);
    QCOMPARE(first.value(QStringLiteral("t")).toString(), QStringLiteral("# Title"));
    QCOMPARE(first.value(QStringLiteral("h")).toString().size(), 16);
    QCOMPARE(root.value(QStringLiteral("outline")).toArray().size(), 1);
}

/**
 * @brief 同一 rev 的载荷逐字节相同；内容一变就重算
 */
void TestMarkdownPreviewController::testDocumentBodyJsonIsCachedPerRev()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString path = writeDoc(m_dir.path(), QStringLiteral("cache.md"),
                                  QStringLiteral("# one\n"));
    QVERIFY(controller.openFile(path).ok);

    const QString first = controller.documentBodyJson();
    QCOMPARE(controller.documentBodyJson(), first);

    controller.setEditorText(QStringLiteral("# one\n\ntwo\n"));
    const QString second = controller.documentBodyJson();
    QVERIFY(second != first);
    QVERIFY(second.contains(QStringLiteral("two")));
    QCOMPARE(controller.documentBodyJson(), second);

    // 视图偏好也进载荷：改了字号就必须重算，否则页面重新 fetch 拿到旧值
    controller.setFontSizePx(21);
    const QString third = controller.documentBodyJson();
    QVERIFY(third.contains(QStringLiteral("21")));
    QVERIFY(third != second);
}

/**
 * @brief 同一类命令在队列里只留最新一条
 *
 * 连续敲键时若每条 updateBlocks 都排队，页面会把中间态全渲染一遍——增量渲染
 * 的全部意义就是只渲染最后一次。
 */
void TestMarkdownPreviewController::testCommandQueueKeepsLatestPerCmd()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString path = writeDoc(m_dir.path(), QStringLiteral("queue.md"),
                                  QStringLiteral("# one\n"));
    QVERIFY(controller.openFile(path).ok);
    controller.notifyWindowOpened();
    drain(controller);

    int queued = 0;
    connect(&controller, &MarkdownPreviewController::commandQueued, &controller,
            [&queued]() { ++queued; });

    controller.setEditorText(QStringLiteral("# one\n\ntwo\n"));
    controller.setEditorText(QStringLiteral("# one\n\ntwo\n\nthree\n"));
    controller.setEditorText(QStringLiteral("# one\n\ntwo\n\nthree\n\nfour\n"));
    QCOMPARE(queued, 3);
    QVERIFY(controller.hasPendingCommand());

    const QStringList cmds = drain(controller);
    QCOMPARE(cmds.size(), 1);
    QCOMPARE(cmds.at(0), QStringLiteral("updateBlocks"));
    QVERIFY(!controller.hasPendingCommand());
}

/**
 * @brief 塌成一条之后，留下的那条带的是最后一次的内容
 */
void TestMarkdownPreviewController::testCollapsedCommandCarriesLatestBlocks()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString path = writeDoc(m_dir.path(), QStringLiteral("latest.md"),
                                  QStringLiteral("# one\n"));
    QVERIFY(controller.openFile(path).ok);
    controller.notifyWindowOpened();
    drain(controller);

    controller.setEditorText(QStringLiteral("# one\n\ntwo\n"));
    controller.setEditorText(QStringLiteral("# one\n\ntwo\n\nfour\n"));
    const QJsonObject command = parseCommand(controller.takeCommand().toUtf8());
    QCOMPARE(command.value(QStringLiteral("cmd")).toString(),
             QStringLiteral("updateBlocks"));
    const QJsonArray blocks =
        command.value(QStringLiteral("args")).toObject()
            .value(QStringLiteral("blocks")).toArray();
    QVERIFY2(!blocks.isEmpty(), "the surviving command must carry blocks");
    QVERIFY2(blocks.at(blocks.size() - 1).toObject()
                 .value(QStringLiteral("t")).toString().contains(QStringLiteral("four")),
             "and it must be the newest edit, not a superseded one");
}

/**
 * @brief setDocument 一进队就把在飞的 updateBlocks 清掉
 *
 * 两者对页面是互斥指令（整篇重建 vs 局部替换），同时送到会打架。
 */
void TestMarkdownPreviewController::testSetDocumentDropsPendingUpdateBlocks()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString a = writeDoc(m_dir.path(), QStringLiteral("a.md"),
                               QStringLiteral("# a\n"));
    QVERIFY(controller.openFile(a).ok);
    controller.notifyWindowOpened();
    drain(controller);

    controller.setEditorText(QStringLiteral("# a\n\nextra\n"));
    QVERIFY(controller.hasPendingCommand());
    // 换文档走 reloadFromDisk：全量 setDocument
    QVERIFY(controller.reloadFromDisk().ok);

    const QStringList cmds = drain(controller);
    QVERIFY2(cmds.contains(QStringLiteral("setDocument")),
             qPrintable(cmds.join(QLatin1Char(','))));
    QVERIFY2(!cmds.contains(QStringLiteral("updateBlocks")),
             "a full rebuild must not leave a stale delta behind");
}

/**
 * @brief 编辑回写只把变化的块交给页面
 */
void TestMarkdownPreviewController::testEditorEditEnqueuesDeltaOnly()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString original = QStringLiteral("one\n\ntwo\n\nthree\n");
    const QString path = writeDoc(m_dir.path(), QStringLiteral("delta.md"),
                                 original);
    QVERIFY(controller.openFile(path).ok);
    controller.notifyWindowOpened();
    drain(controller);

    controller.setEditorText(QStringLiteral("one\n\nTWO\n\nthree\n"));
    const QJsonObject command = parseCommand(controller.takeCommand().toUtf8());
    QCOMPARE(command.value(QStringLiteral("cmd")).toString(),
             QStringLiteral("updateBlocks"));
    const QJsonObject args = command.value(QStringLiteral("args")).toObject();
    QCOMPARE(args.value(QStringLiteral("startIndex")).toInt(), 1);
    QCOMPARE(args.value(QStringLiteral("removeCount")).toInt(), 1);
    const QJsonArray blocks = args.value(QStringLiteral("blocks")).toArray();
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks.at(0).toObject().value(QStringLiteral("t")).toString(),
             QStringLiteral("TWO"));
    QVERIFY(controller.dirty());

    // 改回原文：脏标记自己消失（没有「未保存」的假象），但命令照发——
    // 渲染状态在页面那边，它不知道用户撤销了，不发那条它就还挂着 TWO。
    controller.setEditorText(original);
    QVERIFY(controller.hasPendingCommand());
    const QJsonObject reverted = parseCommand(controller.takeCommand().toUtf8());
    QCOMPARE(reverted.value(QStringLiteral("cmd")).toString(),
             QStringLiteral("updateBlocks"));
    const QJsonObject revertArgs = reverted.value(QStringLiteral("args")).toObject();
    QCOMPARE(revertArgs.value(QStringLiteral("startIndex")).toInt(), 1);
    QCOMPARE(revertArgs.value(QStringLiteral("blocks")).toArray()
                          .at(0).toObject().value(QStringLiteral("t")).toString(),
             QStringLiteral("two"));
    QVERIFY(!controller.dirty());
}

/**
 * @brief 视图偏好既进命令队列，也落进 settings.json 的 markdown 组
 */
void TestMarkdownPreviewController::testViewSettersPersistAndEnqueue()
{
    AWB_SKIP_IF_NO_PREVIEW();
    {
        MarkdownPreviewController controller(m_settings, nullptr);
        controller.setSurfaceReady(true);
        const QString path = writeDoc(m_dir.path(), QStringLiteral("view.md"),
                                      QStringLiteral("# v\n"));
        QVERIFY(controller.openFile(path).ok);
        controller.notifyWindowOpened();
        drain(controller);

        controller.setMode(QStringLiteral("split"));
        controller.setFontSizePx(19);
        controller.setContentWidthPx(900);
        controller.setShowOutline(false);

        QCOMPARE(controller.mode(), QStringLiteral("split"));
        QCOMPARE(controller.fontSizePx(), 19);
        QCOMPARE(controller.contentWidthPx(), 900);
        QVERIFY(!controller.showOutline());

        // 四条 setView 塌成一条，带的是最后一次的四个值
        const QStringList cmds = drain(controller);
        QCOMPARE(cmds.count(QStringLiteral("setView")), 1);

        const MarkdownSettings &saved = m_settings->markdownOptions();
        QCOMPARE(saved.mode, QStringLiteral("split"));
        QCOMPARE(saved.fontSizePx, 19);
        QCOMPARE(saved.contentWidthPx, 900);
        QVERIFY(!saved.showOutline);

        // 落盘由控制器自己完成：预览窗口是低频操作，不该要求用户再点保存
        QFile file(Settings::settingsFilePath());
        QVERIFY2(file.exists(), qPrintable(Settings::settingsFilePath()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject onDisk = QJsonDocument::fromJson(file.readAll())
                                       .object()
                                       .value(QStringLiteral("markdown"))
                                       .toObject();
        file.close();
        QCOMPARE(onDisk.value(QStringLiteral("mode")).toString(),
                 QStringLiteral("split"));
        QCOMPARE(onDisk.value(QStringLiteral("fontSizePx")).toInt(), 19);
        QCOMPARE(onDisk.value(QStringLiteral("contentWidthPx")).toInt(), 900);
        QCOMPARE(onDisk.value(QStringLiteral("showOutline")).toBool(), false);
    }

    // 新建一个控制器就要从盘上把偏好读回来
    Settings reopened(this);
    MarkdownPreviewController restored(&reopened, nullptr);
    QCOMPARE(restored.mode(), QStringLiteral("split"));
    QCOMPARE(restored.fontSizePx(), 19);
    QCOMPARE(restored.contentWidthPx(), 900);
    QVERIFY(!restored.showOutline());
}

/**
 * @brief 非法模式与越界数值被夹住，不会变成页面上一条失效的 CSS
 */
void TestMarkdownPreviewController::testModeRejectsUnknownValue()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString before = controller.mode();

    controller.setMode(QStringLiteral("nonsense"));
    QCOMPARE(controller.mode(), before);

    controller.setFontSizePx(-5);
    QCOMPARE(controller.fontSizePx(), 0);
    controller.setFontSizePx(9999);
    QCOMPARE(controller.fontSizePx(), 48);
    controller.setContentWidthPx(-1);
    QCOMPARE(controller.contentWidthPx(), 0);
    controller.setContentWidthPx(100000);
    QCOMPARE(controller.contentWidthPx(), 4000);
}

/**
 * @brief 搜索、整篇补渲染、序列化与跳转都走同一条命令通道
 */
void TestMarkdownPreviewController::testFindAndScrollCommands()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString path = writeDoc(m_dir.path(), QStringLiteral("find.md"),
                                  QStringLiteral("# one\n"));
    QVERIFY(controller.openFile(path).ok);
    controller.notifyWindowOpened();
    drain(controller);

    controller.requestFind(QStringLiteral("needle"), false, true, true);
    const QJsonObject find = parseCommand(controller.takeCommand().toUtf8());
    QCOMPARE(find.value(QStringLiteral("cmd")).toString(), QStringLiteral("find"));
    const QJsonObject args = find.value(QStringLiteral("args")).toObject();
    QCOMPARE(args.value(QStringLiteral("text")).toString(), QStringLiteral("needle"));
    QVERIFY(!args.value(QStringLiteral("forward")).toBool());
    QVERIFY(args.value(QStringLiteral("caseSensitive")).toBool());
    QVERIFY(args.value(QStringLiteral("restart")).toBool());

    controller.requestFullRender();
    QCOMPARE(parseCommand(controller.takeCommand().toUtf8())
                 .value(QStringLiteral("cmd")).toString(),
             QStringLiteral("renderAll"));

    controller.requestMarkup(QStringLiteral("export"));
    const QJsonObject serialize =
        parseCommand(controller.takeCommand().toUtf8());
    QCOMPARE(serialize.value(QStringLiteral("cmd")).toString(),
             QStringLiteral("serialize"));
    QCOMPARE(serialize.value(QStringLiteral("args")).toObject()
                 .value(QStringLiteral("purpose")).toString(),
             QStringLiteral("export"));

    QString readyHtml;
    QString readyPurpose;
    int ready = 0;
    connect(&controller, &MarkdownPreviewController::markupReady, &controller,
            [&](const QString &html, const QString &purpose) {
                ++ready;
                readyHtml = html;
                readyPurpose = purpose;
            });
    controller.reportMarkup(QStringLiteral("<html>x</html>"));
    QCOMPARE(ready, 1);
    QCOMPARE(readyPurpose, QStringLiteral("export"));
    QCOMPARE(readyHtml, QStringLiteral("<html>x</html>"));
    // purpose 是一次性的：再来一份没有对应请求，用途为空，不会认错接手方
    controller.reportMarkup(QStringLiteral("<html>y</html>"));
    QCOMPARE(ready, 2);
    QVERIFY(readyPurpose.isEmpty());

    controller.scrollToAnchor(QStringLiteral("b2"), 2);
    const QJsonObject scroll = parseCommand(controller.takeCommand().toUtf8());
    QCOMPARE(scroll.value(QStringLiteral("cmd")).toString(),
             QStringLiteral("scrollTo"));
    QCOMPARE(scroll.value(QStringLiteral("args")).toObject()
                 .value(QStringLiteral("anchor")).toString(),
             QStringLiteral("b2"));
}

/**
 * @brief 页面报告各归其位：失败发信号、搜索结果解析、就绪与帧耗时只进日志
 */
void TestMarkdownPreviewController::testReportsAndSignals()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);

    int failed = 0;
    QString failedJson;
    connect(&controller, &MarkdownPreviewController::renderFailed, &controller,
            [&](const QString &json) { ++failed; failedJson = json; });
    controller.reportError(QStringLiteral("{\"blockIndex\":3,\"message\":\"boom\"}"));
    QCOMPARE(failed, 1);
    QVERIFY(failedJson.contains(QStringLiteral("boom")));

    int results = 0;
    int totalSeen = -1;
    int activeSeen = -1;
    connect(&controller, &MarkdownPreviewController::findResult, &controller,
            [&](int total, int active) {
                ++results;
                totalSeen = total;
                activeSeen = active;
            });
    controller.reportFindResult(QStringLiteral("{\"total\":7,\"active\":2}"));
    QCOMPARE(results, 1);
    QCOMPARE(totalSeen, 7);
    QCOMPARE(activeSeen, 2);
    // 不是 JSON 对象就不发信号：页面上混进来一句话也不该把搜索计数清零
    controller.reportFindResult(QStringLiteral("not json"));
    controller.reportFindResult(QStringLiteral("[1,2]"));
    controller.reportFindResult(QString());
    QCOMPARE(results, 1);

    // 就绪与帧耗时只进日志；就绪会把视图与主题两条命令补发一次
    QVERIFY(!controller.hasPendingCommand());
    controller.reportReady(QStringLiteral("{\"workers\":true}"));
    const QStringList cmds = drain(controller);
    QVERIFY2(cmds.contains(QStringLiteral("setView")), qPrintable(cmds.join(QLatin1Char(','))));
    QVERIFY2(cmds.contains(QStringLiteral("setTheme")), qPrintable(cmds.join(QLatin1Char(','))));
    controller.reportFrame(QStringLiteral("{\"parseMs\":12}"));
    QVERIFY(!controller.hasPendingCommand());
}

/**
 * @brief 外部改动：复查置位，保留改动后不再重复提示，重载则换回磁盘那份
 */
void TestMarkdownPreviewController::testDiskNoticeAndReload()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString path = writeDoc(m_dir.path(), QStringLiteral("disk.md"),
                                  QStringLiteral("# on disk\n"));
    QVERIFY(controller.openFile(path).ok);
    controller.notifyWindowOpened();
    QVERIFY(!controller.changedOnDisk());

    controller.setEditorText(QStringLiteral("# my edits\n"));
    QVERIFY(controller.dirty());

    // 只数「提示出现」的跃迁：changedOnDiskChanged 在出现和消失时各发一次，
    // 界面关心的是同一次外部改动会不会被弹第二遍。
    int notices = 0;
    connect(&controller, &MarkdownPreviewController::changedOnDiskChanged,
            &controller, [&controller, &notices]() {
                if (controller.changedOnDisk()) {
                    ++notices;
                }
            });

    // 磁盘那份被别的程序改掉
    writeDoc(m_dir.path(), QStringLiteral("disk.md"),
             QStringLiteral("# changed elsewhere\n"));
    controller.refreshDiskState();
    QCOMPARE(notices, 1);
    QVERIFY(controller.changedOnDisk());
    // 再复查一次不重复发信号（界面不该被同一次改动反复弹提示）
    controller.refreshDiskState();
    QCOMPARE(notices, 1);

    // 用户选「保留我的改动」：提示消失，本地内容不动，且不会再弹回来
    controller.dismissDiskNotice();
    QVERIFY(!controller.changedOnDisk());
    QCOMPARE(controller.editorText(), QStringLiteral("# my edits\n"));
    controller.refreshDiskState();
    QCOMPARE(notices, 1);
    QVERIFY(controller.dirty());

    // 用户选「重载」：本地改动放弃，块与页面都换成磁盘那份
    controller.setEditorText(QStringLiteral("# mine again\n"));
    writeDoc(m_dir.path(), QStringLiteral("disk.md"), QStringLiteral("# theirs\n"));
    controller.refreshDiskState();
    QVERIFY(controller.changedOnDisk());
    QVERIFY(controller.reloadFromDisk().ok);
    QVERIFY(!controller.changedOnDisk());
    QVERIFY(!controller.dirty());
    QCOMPARE(controller.editorText(), QStringLiteral("# theirs\n"));
    const QStringList cmds = drain(controller);
    QVERIFY2(cmds.contains(QStringLiteral("setDocument")),
             qPrintable(cmds.join(QLatin1Char(','))));

    // 文件被删掉：仍然算外部改动，重载失败并给出原因，而不是静默变空文档
    QFile::remove(path);
    controller.refreshDiskState();
    QVERIFY(controller.changedOnDisk());
    QVERIFY(!controller.reloadFromDisk().ok);
    QCOMPARE(controller.editorText(), QStringLiteral("# theirs\n"));
}

/**
 * @brief 关窗放下命令与会话的「窗口态」，会话本身全部保留
 *
 * 会话是应用运行期的记忆：重开窗口直接取回（含未保存改动）——「关窗重开
 * 找回上次的 tab 组」在运行期内因此是免费的。真正清掉一个会话的是关 tab。
 */
void TestMarkdownPreviewController::testWindowCloseKeepsSessions()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QString path = writeDoc(m_dir.path(), QStringLiteral("closing.md"),
                                  QStringLiteral("# c\n"));
    QVERIFY(controller.openFile(path).ok);
    controller.notifyWindowOpened();
    QVERIFY(controller.active());
    drain(controller);
    controller.setEditorText(QStringLiteral("# c\n\ndirtyer\n"));
    QVERIFY(controller.hasPendingCommand());
    const QString docId = controller.activeDocId();
    const QString title = controller.fileTitle();

    controller.notifyWindowClosed();
    QVERIFY(!controller.active());
    // 会话保留：docId、标题、未保存改动与 tab 都还在
    QCOMPARE(controller.activeDocId(), docId);
    QCOMPARE(controller.fileTitle(), title);
    QVERIFY(controller.dirty());
    QCOMPARE(controller.tabs().size(), 1);
    // 在飞命令放下：窗口没了就没有接收方
    QVERIFY(!controller.hasPendingCommand());
    QVERIFY(controller.takeCommand().isEmpty());

    // 重复调用不产生第二次状态变化
    int changes = 0;
    connect(&controller, &MarkdownPreviewController::activeChanged, &controller,
            [&changes]() { ++changes; });
    controller.notifyWindowClosed();
    QCOMPARE(changes, 0);
    controller.notifyWindowOpened();
    QCOMPARE(changes, 1);
    // 开合窗口没有把会话状态弄丢
    QVERIFY(controller.dirty());

    // 真正清掉会话的是关 tab：之后是无文档的空态
    controller.closeSession(docId);
    QVERIFY(controller.activeDocId().isEmpty());
    QVERIFY(controller.filePath().isEmpty());
    QVERIFY(controller.blocks().isEmpty());
    QVERIFY(controller.outline().isEmpty());
    QVERIFY(controller.tabs().isEmpty());
    // 没有会话时的存盘与重载都是失败而不是崩溃
    QVERIFY(!controller.saveNow().ok);
    QVERIFY(!controller.reloadFromDisk().ok);
}

/**
 * @brief 多会话：开/切/关、同路径去重、脏隔离、草稿会话与上限
 *
 * M1 的主轴。逐条锁住：tab 清单的形状、切换时 doc.json 跟着活跃会话走、
 * 旧会话的资源目录仍可解析、脏标记互不污染、关活跃会话的顺位激活、
 * 草稿（虚拟文档）不占 tab、文件会话满 8 拒绝第 9 份。
 */
void TestMarkdownPreviewController::testSessionsOpenSwitchCloseAndDraft()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    controller.notifyWindowOpened();

    const QString a = writeDoc(m_dir.path(), QStringLiteral("one.md"),
                               QStringLiteral("# one\n"));
    const QString b = writeDoc(m_dir.path(), QStringLiteral("two.md"),
                               QStringLiteral("# two\n"));
    QVERIFY(controller.openFile(a).ok);
    const QString idA = controller.activeDocId();
    QVERIFY(controller.openFile(b).ok);
    const QString idB = controller.activeDocId();
    QVERIFY(idA != idB);

    // tab 清单：按打开顺序，认得出标题与激活位
    const QVariantList tabs = controller.tabs();
    QCOMPARE(tabs.size(), 2);
    QCOMPARE(tabs.at(0).toMap().value(QStringLiteral("docId")).toString(),
             idA);
    QCOMPARE(tabs.at(1).toMap().value(QStringLiteral("docId")).toString(),
             idB);
    QCOMPARE(tabs.at(1).toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("two.md"));
    QVERIFY(!tabs.at(0).toMap().value(QStringLiteral("active")).toBool());
    QVERIFY(tabs.at(1).toMap().value(QStringLiteral("active")).toBool());

    // 同路径再开 = 激活既有会话，不新增 tab
    QVERIFY(controller.openFile(a).ok);
    QCOMPARE(controller.activeDocId(), idA);
    QCOMPARE(controller.tabs().size(), 2);

    // 切换：doc.json 跟着活跃会话走；两个会话的资源目录都还能解析
    drain(controller);
    controller.activateSession(idB);
    QCOMPARE(controller.activeDocId(), idB);
    QCOMPARE(controller.filePath(), QFileInfo(b).absoluteFilePath());
    QVERIFY(controller.documentBodyJson().contains(QStringLiteral("two.md")));
    QVERIFY(!controller.assetRootFor(idA).isEmpty());
    QVERIFY(!controller.assetRootFor(idB).isEmpty());
    QVERIFY(controller.assetRootFor(QStringLiteral("ghost")).isEmpty());
    // 切会话发的是整篇重建
    const QStringList switched = drain(controller);
    QVERIFY2(switched.contains(QStringLiteral("setDocument")),
             qPrintable(switched.join(QLatin1Char(','))));

    // 脏隔离：只有被编辑的那个会话脏，tab 徽标跟着它
    controller.setEditorText(QStringLiteral("# two edited\n"));
    QVERIFY(controller.dirty());
    controller.activateSession(idA);
    QVERIFY(!controller.dirty());
    const QVariantList afterEdit = controller.tabs();
    QVERIFY(!afterEdit.at(0).toMap().value(QStringLiteral("dirty")).toBool());
    QVERIFY(afterEdit.at(1).toMap().value(QStringLiteral("dirty")).toBool());
    controller.activateSession(idB);
    QVERIFY(controller.dirty());

    // 关掉活跃会话：顺位激活最后打开的那个；关不存在的 id 是无效操作
    controller.closeSession(idB);
    QCOMPARE(controller.activeDocId(), idA);
    QCOMPARE(controller.tabs().size(), 1);
    controller.closeSession(idB);
    QCOMPARE(controller.tabs().size(), 1);

    // 草稿会话：幂等、虚拟文档、可编辑、永不脏、不占 tab
    const QString draftId = controller.openDraftSession();
    QCOMPARE(draftId, QStringLiteral("draft"));
    QCOMPARE(controller.openDraftSession(), draftId);
    controller.setDraftText(QStringLiteral("# draft body\n"));
    controller.activateSession(draftId);
    QVERIFY(controller.isDraftActive());
    QCOMPARE(controller.fileTitle(), QStringLiteral("Draft"));
    QVERIFY(controller.canEdit());
    QVERIFY(!controller.dirty());
    QVERIFY(controller.documentBodyJson().contains(QStringLiteral("draft body")));
    QCOMPARE(controller.tabs().size(), 1);
    QVERIFY(controller.assetRootFor(draftId).isEmpty());   // 无相对资源
    // 关草稿是无效操作：它是提示词形态的本体，不由 tab 关闭
    controller.closeSession(draftId);
    QVERIFY(controller.isDraftActive());

    // 文件会话上限：满 8 拒绝第 9 份，不静默淘汰
    for (int i = 0; i < MarkdownPreviewController::kMaxFileSessions - 1; ++i) {
        const QString p = writeDoc(
            m_dir.path(),
            QStringLiteral("cap%1.md").arg(i), QStringLiteral("# c\n"));
        QVERIFY2(controller.openFile(p).ok, qPrintable(p));
    }
    QCOMPARE(controller.tabs().size(), MarkdownPreviewController::kMaxFileSessions);
    const QString ninth = writeDoc(m_dir.path(), QStringLiteral("ninth.md"),
                                   QStringLiteral("# 9\n"));
    const OpResult capped = controller.openFile(ninth);
    QVERIFY(!capped.ok);
    QVERIFY(capped.error.contains(QStringLiteral("Too many")));
    QCOMPARE(controller.tabs().size(),
             MarkdownPreviewController::kMaxFileSessions);
}

/**
 * @brief 页面地址只有一个来源，后缀清单只有一处判定
 *
 * 两条都是「界面拿不到第二套真相」的护栏：视图的 url 由 pageUrl() 给，QML 不
 * 自己拼 awbmd://（拼错的表现是页面 404，而构建与测试全绿）；右键菜单出不出
 * 「Preview Markdown」用 canPreviewPath()，与 openFile() 的拒绝共用同一个后缀
 * 判据，否则迟早出现「菜单里能点、点开被拒」。
 */
void TestMarkdownPreviewController::testPageUrlAndPreviewEligibility()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);

    QCOMPARE(controller.pageUrl().toString(),
             QStringLiteral("awbmd://runtime/app/shell.html"));

    // 后缀清单（大小写不敏感）；`\z` 锚定，所以 .md.bak 不算 Markdown
    QVERIFY(controller.canPreviewPath(QStringLiteral("/tmp/NOTE.MD")));
    QVERIFY(controller.canPreviewPath(QStringLiteral("C:/x/readme.markdown")));
    QVERIFY(controller.canPreviewPath(QStringLiteral("notes.mdown")));
    QVERIFY(controller.canPreviewPath(QStringLiteral("notes.mdtxt")));
    QVERIFY(controller.canPreviewPath(QStringLiteral("notes.text")));
    QVERIFY(!controller.canPreviewPath(QStringLiteral("notes.txt")));
    QVERIFY(!controller.canPreviewPath(QStringLiteral("notes.md.bak")));
    QVERIFY(!controller.canPreviewPath(QStringLiteral("readme")));
    QVERIFY(!controller.canPreviewPath(QString()));

    // 表面没装上（Qt 5 与关掉 WebEngine 的构建）时后缀再对也不给预览：
    // 菜单项整批消失，而不是点开一个开不了的窗。
    MarkdownPreviewController bare(m_settings, nullptr);
    QVERIFY(!bare.available());
    QVERIFY(!bare.canPreviewPath(QStringLiteral("/tmp/NOTE.MD")));
}

/**
 * @brief 序列化件的落点：默认在文档旁边、用户挑过就换目录、外部打开另有去处
 *
 * 这段是纯层的「文件 IO」，而 QML 侧一行都不碰磁盘——所以「写到哪、写不写得成」
 * 只能在这里验。三条路各自的后果不同：导出件里的图片是相对引用，落到别处就
 * 显示不出来；「用外部浏览器打开」必须写真实文件，因为外部浏览器不认识
 * awbmd://；两者都没有落点时必须报原因，不能静默什么都不写。
 */
void TestMarkdownPreviewController::testExportLandingWritesFiles()
{
    AWB_SKIP_IF_NO_PREVIEW();
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    controller.notifyWindowOpened();

    QSignalSpy written(&controller, &MarkdownPreviewController::markupWritten);
    QSignalSpy failed(&controller, &MarkdownPreviewController::markupWriteFailed);
    QSignalSpy ready(&controller, &MarkdownPreviewController::markupReady);

    const QString html = QStringLiteral("<html><body><p>exported</p></body></html>");

    // 没有文档 = 没有落点：失败要有 purpose，界面才知道该收回哪个按钮的等待态
    controller.requestMarkup(QStringLiteral("export"));
    const QStringList queued = drain(controller);
    QCOMPARE(queued.size(), 1);
    QCOMPARE(queued.at(0), QStringLiteral("serialize"));
    controller.reportMarkup(html);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("export"));
    QVERIFY(written.isEmpty());

    // 有文档：默认落在文档旁边，名字是主名换 .html
    const QString docPath = writeDoc(m_dir.path(), QStringLiteral("note.md"),
                                     QStringLiteral("# n\n"));
    QVERIFY(controller.openFile(docPath).ok);
    QCOMPARE(controller.exportFileName(), QStringLiteral("note.html"));
    QVERIFY(controller.exportDirectory().isEmpty());
    controller.requestMarkup(QStringLiteral("export"));
    drain(controller);
    controller.reportMarkup(html);
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(1).toString(), QStringLiteral("export"));
    const QString beside = written.at(0).at(0).toString();
    QCOMPARE(beside, QDir(m_dir.path()).filePath(QStringLiteral("note.html")));
    QFile besideFile(beside);
    QVERIFY(besideFile.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(besideFile.readAll()), html);

    // 用户挑过目录就写挑的那里（图片取不到是他知情选择的后果，不是程序的）
    const QString other = QDir(m_dir.path()).filePath(QStringLiteral("elsewhere"));
    QVERIFY(QDir().mkpath(other));
    controller.setExportDirectory(other);
    QCOMPARE(controller.exportDirectory(), other);
    written.clear();
    controller.requestMarkup(QStringLiteral("export"));
    drain(controller);
    controller.reportMarkup(html);
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(0).toString(),
             QDir(other).filePath(QStringLiteral("note.html")));

    // 外部打开不看用户挑的目录：临时件一律进数据根的 preview/
    written.clear();
    controller.requestMarkup(QStringLiteral("browser"));
    drain(controller);
    controller.reportMarkup(html);
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(1).toString(), QStringLiteral("browser"));
    const QString temp = written.at(0).at(0).toString();
    QCOMPARE(temp, Paths::markdownPreviewDir() + QStringLiteral("/note.html"));
    QVERIFY(QFile::exists(temp));

    // 目录存在却写不进去（同名是个文件）：报失败原因，不抛异常也不写坏东西
    const QString blocked = QDir(m_dir.path()).filePath(QStringLiteral("blocked"));
    {
        QFile notADir(blocked);
        QVERIFY(notADir.open(QIODevice::WriteOnly));
        notADir.write("x");
        notADir.close();
    }
    failed.clear();
    controller.setExportDirectory(blocked);
    controller.requestMarkup(QStringLiteral("export"));
    drain(controller);
    controller.reportMarkup(html);
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.at(0).at(1).toString().isEmpty());
    // 没有新增落盘：计数的还是上面 browser 那一次
    QCOMPARE(written.count(), 1);

    // 关掉这个 tab：导出目录跟着会话放下——留着它，下一份 README 会被
    // 导出到别的项目里
    controller.closeSession(controller.activeDocId());
    QVERIFY(controller.exportDirectory().isEmpty());
    QCOMPARE(controller.exportFileName(), QStringLiteral("Markdown.html"));
}

/**
 * @brief QML 用到的每个方法都是 Q_INVOKABLE
 *
 * 这条不是形式：裸方法不在 meta-object 方法表里，QML 调用即抛
 * 「is not a function」，而页面加载冒烟从不点击，缺陷能长期静默存在。
 * 只有 Q_INVOKABLE（及槽/信号）会进这张表，所以「名字在表里且是普通方法」
 * 就是可调用性的准确判据。
 */
void TestMarkdownPreviewController::testQmlCallableMethodsAreInvokable()
{
    MarkdownPreviewController controller(m_settings, nullptr);
    controller.setSurfaceReady(true);
    const QStringList names = {
        QStringLiteral("openFile"), QStringLiteral("notifyWindowOpened"),
        QStringLiteral("notifyWindowClosed"), QStringLiteral("setEditorText"),
        QStringLiteral("editorText"), QStringLiteral("saveNow"),
        QStringLiteral("reloadFromDisk"), QStringLiteral("dismissDiskNotice"),
        QStringLiteral("refreshDiskState"), QStringLiteral("documentBodyJson"),
        QStringLiteral("takeCommand"), QStringLiteral("hasPendingCommand"),
        QStringLiteral("reportReady"), QStringLiteral("reportFrame"),
        QStringLiteral("reportError"), QStringLiteral("reportMarkup"),
        QStringLiteral("requestMarkup"), QStringLiteral("requestFullRender"),
        QStringLiteral("requestFind"), QStringLiteral("reportFindResult"),
        QStringLiteral("scrollToAnchor"), QStringLiteral("setMode"),
        QStringLiteral("setFontSizePx"), QStringLiteral("setContentWidthPx"),
        QStringLiteral("setShowOutline"),
        // 窗口那两份接线新用的：视图的 url 绑 pageUrl()，导出前设落点
        QStringLiteral("pageUrl"), QStringLiteral("setExportDirectory"),
        // 会话管理（M1）：tab 条、草稿会话
        QStringLiteral("activateSession"), QStringLiteral("closeSession"),
        QStringLiteral("openDraftSession"), QStringLiteral("setDraftText"),
    };
    // 表里的普通方法（Method 型、公开）即 Q_INVOKABLE；信号另算
    const QMetaObject *meta = controller.metaObject();
    QStringList invocables;
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() == QMetaMethod::Method
            && method.access() == QMetaMethod::Public) {
            invocables << QString::fromLatin1(method.name());
        }
    }
    for (const QString &name : names) {
        QVERIFY2(invocables.contains(name),
                 qPrintable(QStringLiteral("%1 is not callable from QML").arg(name)));
    }

    // 可写属性一律带 WRITE：QML 里 `markdown.mode = 'split'` 的写法靠这个
    const QList<QByteArray> properties = { "mode", "fontSizePx",
                                           "contentWidthPx", "showOutline" };
    for (const QByteArray &name : properties) {
        const int index = meta->indexOfProperty(name.constData());
        QVERIFY2(index >= 0,
                 qPrintable(QStringLiteral("no Q_PROPERTY named %1")
                                .arg(QString::fromLatin1(name))));
        const QMetaProperty property = meta->property(index);
        QVERIFY2(property.isWritable(),
                 qPrintable(QStringLiteral("%1 has no WRITE").arg(
                                QString::fromLatin1(name))));
    }

    // QML 侧真实解析路径：按名字 invoke 必须成功（复现点击时的查找）
    QVERIFY(QMetaObject::invokeMethod(&controller, "setMode",
                                      Q_ARG(QString, QStringLiteral("source"))));
    QCOMPARE(controller.mode(), QStringLiteral("source"));
    QVERIFY(QMetaObject::invokeMethod(&controller, "setFontSizePx",
                                      Q_ARG(int, 17)));
    QCOMPARE(controller.fontSizePx(), 17);
}

#include "tst_markdownpreviewcontroller.moc"
