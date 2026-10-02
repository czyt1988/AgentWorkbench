#include "awbtest.h"

#include "markdown/MarkdownChunker.h"
#include "markdown/MarkdownRuntime.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtTest>

using awb::markdown::MarkdownBlock;
using awb::markdown::MarkdownChunker;
using awb::markdown::MarkdownRuntime;

namespace {

QByteArray readResource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return bytes;
}

// 纯 ASCII 判定：没有任何码点超过 0x7F 时为真。
bool isAsciiText(const QString &text)
{
    for (const QChar &c : text) {
        if (c.unicode() > 0x7F) {
            return false;
        }
    }
    return true;
}

// 清单是资产与代码之间唯一的契约：它说有什么、在哪、什么版本，
// 运行时按它供文件，测试按它核对文件真的在。
QJsonObject manifest()
{
    const QByteArray bytes =
        readResource(QStringLiteral(":/markdown/vendor/manifest.json"));
    if (bytes.isEmpty()) {
        return QJsonObject();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    return doc.isObject() ? doc.object() : QJsonObject();
}

// 两个名字集合的差异，写成一条能直接读懂的消息；一致时返回空串。
// 取空串而不是返回 bool，是因为断言失败时「哪边少了哪个名字」正是唯一
// 有用的信息，而 QCOMPARE 打印两个 QSet 的排序还得不一致才看得出区别。
QString nameSetDiff(const QSet<QString> &expected, const QSet<QString> &actual,
                    const QString &label)
{
    QStringList missing;
    QStringList extra;
    for (const QString &name : expected) {
        if (!actual.contains(name)) {
            missing.append(name);
        }
    }
    for (const QString &name : actual) {
        if (!expected.contains(name)) {
            extra.append(name);
        }
    }
    missing.sort();
    extra.sort();
    if (missing.isEmpty() && extra.isEmpty()) {
        return QString();
    }
    QString message = label;
    if (!missing.isEmpty()) {
        message += QStringLiteral(" lacks %1").arg(missing.join(QStringLiteral(", ")));
    }
    if (!extra.isEmpty()) {
        message += QStringLiteral(", but also has %1")
                       .arg(extra.join(QStringLiteral(", ")));
    }
    return message;
}

// 造一个块（只填切分保真/大纲逻辑要读的字段）
MarkdownBlock blockOf(int index, const QString &text)
{
    MarkdownBlock block;
    block.index = index;
    block.text = text;
    block.hash = MarkdownChunker::blockHash(text);
    return block;
}

// query 里取一个已解码的参数值。不用 QUrl::queryItemValue / QUrlQuery::queryItemValue：
// 两个都在 Qt 6.4 起被弃用，而本仓库要在 Qt 5.15 与 Qt 6 两条路线上都干净编译，
// 手工拆 "&"/"=" 加一次 fromPercentEncoding 是三行代码，且两版行为一致。
QString queryValue(const QUrl &url, const QString &key)
{
    const QString prefix = key + QLatin1Char('=');
    const QString query = url.query(QUrl::FullyEncoded);
    for (const QString &pair : query.split(QLatin1Char('&'))) {
        if (pair.startsWith(prefix)) {
            return QUrl::fromPercentEncoding(pair.mid(prefix.size()).toUtf8());
        }
    }
    return QString();
}

} // namespace

/**
 * @brief MarkdownRuntime 的测试类：awbmd:// 的 URL 布局、资源映射、MIME 表、
 *        vendor 清单完整性、大纲提取与命令 JSON
 */
class TestMarkdownRuntime : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testUrlLayout();
    void testResourcePathMapping();
    void testMimeTypeTable();
    void testBeaconPayloadRoundTrip();
    void testCommandJson();
    void testProtocolNameTables();
    void testPageRuntimeMatchesProtocolTables();
    void testManifestMatchesFiles();
    void testVendorFilesResolveAtTheirUrls();
    void testRendererVersionText();
    void testHeadingLevelAndOutline();
    void testPlainHeadingText();
    void testPageStringsAreAsciiAndComplete();
    void testCssVariableKeys();
};

AWB_TEST(TestMarkdownRuntime)

/**
 * @brief URL 的形状就是适配器的路由表，写错一处就是一个 404
 */
void TestMarkdownRuntime::testUrlLayout()
{
    QCOMPARE(QString::fromLatin1(MarkdownRuntime::kScheme),
             QStringLiteral("awbmd"));
    QCOMPARE(QString::fromLatin1(MarkdownRuntime::kHost),
             QStringLiteral("runtime"));

    QCOMPARE(MarkdownRuntime::runtimeUrl(QStringLiteral("/doc.json")).toString(),
             QStringLiteral("awbmd://runtime/doc.json"));
    QCOMPARE(MarkdownRuntime::vendorUrl(QStringLiteral("katex"),
                                        QStringLiteral("katex.min.js")).toString(),
             QStringLiteral("awbmd://runtime/vendor/katex/katex.min.js"));
    QCOMPARE(MarkdownRuntime::appUrl(QStringLiteral("renderer.js")).toString(),
             QStringLiteral("awbmd://runtime/app/renderer.js"));
    QCOMPARE(MarkdownRuntime::documentUrl(7).toString(),
             QStringLiteral("awbmd://runtime/doc.json?rev=7"));

    // 文档相对资源带 docId：换文档后旧 URL 必须失效，而不是命中缓存
    const QUrl asset = MarkdownRuntime::assetUrl(
        QStringLiteral("d1"), QStringLiteral("img/pic.png"));
    QCOMPARE(asset.toString(),
             QStringLiteral("awbmd://runtime/asset/d1/img/pic.png"));

    // 信标：kind 与 payload 都在 query 里，payload 里的保留字符必须被编码
    const QUrl beacon = MarkdownRuntime::eventUrl(
        QStringLiteral("ready"), QStringLiteral("{\"a\":1}"));
    QCOMPARE(beacon.toString().left(32),
             QStringLiteral("awbmd://runtime/event?k=ready&p="));
    QCOMPARE(queryValue(beacon, QStringLiteral("k")),
             QStringLiteral("ready"));
    QCOMPARE(queryValue(beacon, QStringLiteral("p")),
             QStringLiteral("{\"a\":1}"));
}

/**
 * @brief awbmd 路径与 Qt 资源路径是同一棵树的两个名字
 *
 * 这个映射是供文件的全部逻辑；它错了就是「页面拿得到 js 但拿不到字体」
 * 这类只在真实渲染时才暴露的问题，所以在这里按形状直接测，而
 * testVendorFilesResolveAtTheirUrls 用同一个返回值真的 QFile 打开一遍——
 * 'qrc:/…' 这种写法在 Windows 上形状完全正确、却打不开，只测字符串会漏。
 */
void TestMarkdownRuntime::testResourcePathMapping()
{
    QCOMPARE(MarkdownRuntime::resourcePathForPath(QStringLiteral("/vendor/katex/katex.min.css")),
             QStringLiteral(":/markdown/vendor/katex/katex.min.css"));
    // 前导斜杠可选
    QCOMPARE(MarkdownRuntime::resourcePathForPath(QStringLiteral("app/renderer.js")),
             QStringLiteral(":/markdown/app/renderer.js"));
    // 目录穿越一律拒绝：scheme handler 只能吐出这棵树里的东西
    QVERIFY(MarkdownRuntime::resourcePathForPath(
                QStringLiteral("/vendor/../../windows/win.ini")).isEmpty());
    QVERIFY(MarkdownRuntime::resourcePathForPath(QStringLiteral("a/../b")).isEmpty());
    // 空路径给资源根，handler 据此回 404 而不是读到整个前缀
    QCOMPARE(MarkdownRuntime::resourcePathForPath(QString()),
             QStringLiteral(":/markdown/"));
}

/**
 * @brief MIME 表覆盖脚本、样式、字体与图片；未知扩展留空让调用方 404
 *
 * 猜错 Content-Type 的表现是「脚本当文本返回、页面白屏且没有任何错误」，
 * 所以这张表必须有测试而不是靠人记。
 */
void TestMarkdownRuntime::testMimeTypeTable()
{
    QVERIFY(MarkdownRuntime::mimeTypeForPath(QStringLiteral("a/shell.html"))
                .startsWith(QStringLiteral("text/html")));
    QVERIFY(MarkdownRuntime::mimeTypeForPath(QStringLiteral("a/b.js"))
                .contains(QStringLiteral("javascript")));
    QVERIFY(MarkdownRuntime::mimeTypeForPath(QStringLiteral("a/b.css"))
                .startsWith(QStringLiteral("text/css")));
    QVERIFY(MarkdownRuntime::mimeTypeForPath(QStringLiteral("a/b.json"))
                .startsWith(QStringLiteral("application/json")));
    QCOMPARE(MarkdownRuntime::mimeTypeForPath(QStringLiteral("f.woff2")),
             QStringLiteral("font/woff2"));
    QCOMPARE(MarkdownRuntime::mimeTypeForPath(QStringLiteral("f.svg")),
             QStringLiteral("image/svg+xml"));
    // 大小写不敏感
    QCOMPARE(MarkdownRuntime::mimeTypeForPath(QStringLiteral("F.PNG")),
             QStringLiteral("image/png"));
    QVERIFY(MarkdownRuntime::mimeTypeForPath(QStringLiteral("no-dot")).isEmpty());
    QVERIFY(MarkdownRuntime::mimeTypeForPath(QStringLiteral("a.bin")).isEmpty());
}

/**
 * @brief 信标 payload 的编解码往返，以及超长截断不产生半个字符
 */
void TestMarkdownRuntime::testBeaconPayloadRoundTrip()
{
    const QString text = QStringLiteral("事件：\u4ec5\u4f9b测试 · mixed ascii");
    const QUrl url = MarkdownRuntime::eventUrl(QStringLiteral("perf"), text);
    QCOMPARE(MarkdownRuntime::payloadFromBeacon(
                 queryValue(url, QStringLiteral("p")).toUtf8()),
             text);

    // 手工截到任意字节（正好切进一个多字节字符中间）也必须能解回来
    const QByteArray bytes = text.toUtf8();
    for (int cut = 1; cut < bytes.size(); ++cut) {
        QVERIFY(!MarkdownRuntime::payloadFromBeacon(bytes.left(cut)).isEmpty());
    }

    // 超过上限的信标在生成时就被截掉并发警告，不会撑爆 URL
    const QString huge(9000, QLatin1Char('x'));
    const QUrl hugeUrl = MarkdownRuntime::eventUrl(QStringLiteral("err"), huge);
    QVERIFY(queryValue(hugeUrl, QStringLiteral("p")).size()
            <= MarkdownRuntime::kEventPayloadBytes);
}

/**
 * @brief 命令 JSON 的形状：cmd/seq/args，紧凑且 ASCII 安全
 */
void TestMarkdownRuntime::testCommandJson()
{
    QJsonObject args;
    args.insert(QStringLiteral("rev"), 3);
    args.insert(QStringLiteral("text"), QStringLiteral("查找"));
    const QString json =
        MarkdownRuntime::commandJson(QStringLiteral("find"), 12, args);

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QVERIFY(doc.isObject());
    const QJsonObject object = doc.object();
    QCOMPARE(object.value(QStringLiteral("cmd")).toString(),
             QStringLiteral("find"));
    QCOMPARE(object.value(QStringLiteral("seq")).toInt(), 12);
    QCOMPARE(object.value(QStringLiteral("args")).toObject()
                 .value(QStringLiteral("rev")).toInt(), 3);
    // 非 ASCII 不能变成 \uXXXX 之外的形态；紧凑格式里换行都不该有
    QVERIFY(!json.contains(QLatin1Char('\n')));
}

/**
 * @brief 协议名表：非空、不重复，且把页面侧认识的名字逐个钉住
 *
 * C++ 与页面 JS 之间是字符串协议，没有编译期检查：改名时一边改了、另一边没改，
 * 表现是「命令发出去了、页面什么都不做」。这两张表是唯一权威，测试把每个名字
 * 写死在这里，重命名必须同时改 bridge.js 的分支与本用例，漂移就只能以失败
 * 收场，不会静默。
 */
void TestMarkdownRuntime::testProtocolNameTables()
{
    const QStringList commands = MarkdownRuntime::commandNames();
    const QStringList kinds = MarkdownRuntime::eventKinds();

    QCOMPARE(commands, QStringList({
        QStringLiteral("setDocument"), QStringLiteral("updateBlocks"),
        QStringLiteral("setView"),     QStringLiteral("setTheme"),
        QStringLiteral("scrollTo"),    QStringLiteral("find"),
        QStringLiteral("renderAll"),   QStringLiteral("serialize") }));
    QCOMPARE(kinds, QStringList({
        QStringLiteral("ready"),  QStringLiteral("frame"),
        QStringLiteral("error"),  QStringLiteral("find") }));

    QCOMPARE(QSet<QString>(commands.begin(), commands.end()).size(),
             commands.size());
    QCOMPARE(QSet<QString>(kinds.begin(), kinds.end()).size(), kinds.size());
}

/**
 * @brief 页面 JS 的分发分支与信标名，必须和 C++ 的两张表一一对应
 *
 * 上一条用例只钉住 C++ 侧；这条把 `runtime/bridge.js` 真的读进来比对，于是
 * 「一边改了、另一边没改」在 tst_markdown 里就红，而不是拖到页面冒烟、甚至
 * 拖到用户看到「命令发出去了、页面什么都不做」。之所以能这么读：页面运行时
 * 与 vendor 资产嵌在同一个 /markdown 前缀下，测试目标里也嵌了一份。
 *
 * 比的是集合而不是顺序——switch 分支的书写顺序与协议无关。
 */
void TestMarkdownRuntime::testPageRuntimeMatchesProtocolTables()
{
    const QByteArray source = readResource(
        QStringLiteral(":/markdown/app/bridge.js"));
    QVERIFY2(!source.isEmpty(),
             "the page runtime must be embedded for tst_markdown: "
             ":/markdown/app/bridge.js");
    const QString text = QString::fromUtf8(source);

    QSet<QString> pageCommands;
    const QRegularExpression commandRule(
        QStringLiteral("case\\s+\"([A-Za-z]+)\"\\s*:"));
    QRegularExpressionMatchIterator matches = commandRule.globalMatch(text);
    while (matches.hasNext()) {
        pageCommands.insert(matches.next().captured(1));
    }
    QVERIFY2(!pageCommands.isEmpty(), "no dispatch branches found in bridge.js");
    const QString commandDiff = nameSetDiff(
        QSet<QString>(MarkdownRuntime::commandNames().begin(),
                      MarkdownRuntime::commandNames().end()),
        pageCommands,
        QStringLiteral("bridge.js dispatch"));
    QVERIFY2(commandDiff.isEmpty(), qPrintable(commandDiff));

    QSet<QString> pageKinds;
    const QRegularExpression beaconRule(
        QStringLiteral("beacon\\(\\s*\"([a-z]+)\""));
    matches = beaconRule.globalMatch(text);
    while (matches.hasNext()) {
        pageKinds.insert(matches.next().captured(1));
    }
    QVERIFY2(!pageKinds.isEmpty(), "no beacons found in bridge.js");
    const QString kindDiff = nameSetDiff(
        QSet<QString>(MarkdownRuntime::eventKinds().begin(),
                      MarkdownRuntime::eventKinds().end()),
        pageKinds,
        QStringLiteral("bridge.js beacons"));
    QVERIFY2(kindDiff.isEmpty(), qPrintable(kindDiff));
}

/**
 * @brief vendor 清单里登记的每个文件都真在资源树里，且 sha256 对得上
 *
 * 这条把「入库资产被改动」和「构建没把子目录嵌进来」两类问题一次性挡住。
 * 清单给 dir 而不是从 npm 名推目录（highlight.js 的目录是 highlight-js），
 * 推出来的 URL 与真实 URL 不一致，表现正是下面第二类的 404。
 */
void TestMarkdownRuntime::testManifestMatchesFiles()
{
    const QJsonObject root = manifest();
    QVERIFY2(!root.isEmpty(), "the embedded vendor manifest must exist");
    const QJsonArray libs = root.value(QStringLiteral("libs")).toArray();
    QVERIFY(libs.size() >= 4);

    for (const QJsonValue &libValue : libs) {
        const QJsonObject lib = libValue.toObject();
        const QString name = lib.value(QStringLiteral("name")).toString();
        const QString dir = lib.value(QStringLiteral("dir")).toString();
        QVERIFY2(!dir.isEmpty(),
                 qPrintable(QStringLiteral("%1: manifest entries need a dir").arg(name)));

        // files 与 fonts 同构：都是 { path, bytes, sha256 }，path 相对该库目录
        const QJsonArray files = lib.value(QStringLiteral("files")).toArray();
        QVERIFY2(!files.isEmpty(),
                 qPrintable(QStringLiteral("%1: no files listed").arg(name)));
        QJsonArray entries = files;
        const QJsonArray fonts = lib.value(QStringLiteral("fonts")).toArray();
        for (const QJsonValue &font : fonts) {
            entries.append(font);
        }
        for (const QJsonValue &fileValue : entries) {
            const QJsonObject file = fileValue.toObject();
            const QString rel = file.value(QStringLiteral("path")).toString();
            const QString expected =
                file.value(QStringLiteral("sha256")).toString();
            QVERIFY2(expected.size() == 64,
                     qPrintable(QStringLiteral("%1/%2: sha256 missing").arg(dir, rel)));
            const QString url =
                QStringLiteral(":/markdown/vendor/%1/%2").arg(dir, rel);
            const QByteArray bytes = readResource(url);
            QVERIFY2(!bytes.isEmpty(),
                     qPrintable(QStringLiteral("missing embedded asset: %1").arg(url)));
            QCOMPARE(qint64(bytes.size()),
                     file.value(QStringLiteral("bytes")).toDouble());
            const QString actual = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            QVERIFY2(actual.compare(expected, Qt::CaseInsensitive) == 0,
                     qPrintable(QStringLiteral("%1 changed: %2 != %3")
                                    .arg(url, actual, expected)));
        }
    }
}

/**
 * @brief 代码里算出来的 URL 与嵌入资源的 URL 是同一个字符串
 *
 * 清单核对用的是清单里的 dir，handler 用的是 vendorUrl()。两条路必须汇到
 * 同一个 qrc 路径上，否则测试全绿而页面 404。
 */
void TestMarkdownRuntime::testVendorFilesResolveAtTheirUrls()
{
    const struct
    {
        const char *dir;
        const char *file;
    } probes[] = {
        { "markdown-it", "markdown-it.umd.min.js" },
        { "katex", "katex.min.js" },
        { "katex", "katex.min.css" },
        { "katex", "fonts/KaTeX_Main-Regular.woff2" },
        { "mermaid", "mermaid.min.js" },
        { "highlight-js", "highlight.min.js" },
    };
    for (const auto &probe : probes) {
        const QUrl url = MarkdownRuntime::vendorUrl(
            QLatin1String(probe.dir), QLatin1String(probe.file));
        const QString resource = MarkdownRuntime::resourcePathForPath(url.path());
        QVERIFY2(!resource.isEmpty(), qPrintable(url.toString()));
        QVERIFY2(!readResource(resource).isEmpty(),
                 qPrintable(QStringLiteral("%1 -> %2 is not embedded")
                                .arg(url.toString(), resource)));
        QVERIFY(!MarkdownRuntime::mimeTypeForPath(url.path()).isEmpty());
    }
}

/**
 * @brief 版本串来自清单，不是代码里手写的第二个真相
 */
void TestMarkdownRuntime::testRendererVersionText()
{
    const QString text = MarkdownRuntime::rendererVersionText();
    QVERIFY2(text.contains(QStringLiteral("markdown-it")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("KaTeX")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("Mermaid")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("highlight.js")), qPrintable(text));
    // 与清单里的版本号一致
    const QJsonArray libs = manifest().value(QStringLiteral("libs")).toArray();
    for (const QJsonValue &libValue : libs) {
        const QJsonObject lib = libValue.toObject();
        QVERIFY2(text.contains(lib.value(QStringLiteral("version")).toString()),
                 qPrintable(QStringLiteral("%1 missing from the version string")
                                .arg(lib.value(QStringLiteral("name")).toString())));
    }
}

/**
 * @brief 标题级别与大纲：ATX、setext、代码块里的假标题都不误判
 *
 * 大纲是「点一下跳到那一块」的入口，锚点必须与页面 DOM 的 section id 同名，
 * 所以这里同时断言 anchor 的形状（b<块索引>）。
 */
void TestMarkdownRuntime::testHeadingLevelAndOutline()
{
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("# One"))), 1);
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("   ###  Three"))), 3);
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("#hashtag"))), 0);
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("####### seven"))), 0);
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("plain text"))), 0);
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("Title\n==="))), 1);
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("Sub\n---"))), 2);
    // 分隔线自己成块时不是标题
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("---"))), 0);
    QCOMPARE(MarkdownRuntime::headingLevelOf(blockOf(0, QStringLiteral("```\n# c\n```"))), 0);

    const QVector<MarkdownBlock> blocks = {
        blockOf(0, QStringLiteral("# Alpha")),
        blockOf(1, QStringLiteral("body")),
        blockOf(2, QStringLiteral("## Beta *em* `code`")),
        blockOf(3, QStringLiteral("Gamma\n=====")),
    };
    const QVariantList outline = MarkdownRuntime::outlineFromBlocks(blocks);
    QCOMPARE(outline.size(), 3);
    const QVariantMap first = outline.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("level")).toInt(), 1);
    QCOMPARE(first.value(QStringLiteral("text")).toString(), QStringLiteral("Alpha"));
    QCOMPARE(first.value(QStringLiteral("anchor")).toString(), QStringLiteral("b0"));
    QCOMPARE(first.value(QStringLiteral("blockIndex")).toInt(), 0);
    QCOMPARE(outline.at(1).toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Beta em code"));
    QCOMPARE(outline.at(2).toMap().value(QStringLiteral("level")).toInt(), 1);
    QCOMPARE(outline.at(2).toMap().value(QStringLiteral("anchor")).toString(),
             QStringLiteral("b3"));
}

/**
 * @brief 大纲文本去行内标记：链接取文字、图片取 alt、HTML 标签整个去掉
 */
void TestMarkdownRuntime::testPlainHeadingText()
{
    QCOMPARE(MarkdownRuntime::plainHeadingText(
                 QStringLiteral("[name](https://example.com)")),
             QStringLiteral("name"));
    QCOMPARE(MarkdownRuntime::plainHeadingText(
                 QStringLiteral("![alt](pic.png)")),
             QStringLiteral("alt"));
    QCOMPARE(MarkdownRuntime::plainHeadingText(QStringLiteral("**bold** and *em*")),
             QStringLiteral("bold and em"));
    QCOMPARE(MarkdownRuntime::plainHeadingText(QStringLiteral("`code`")),
             QStringLiteral("code"));
    QCOMPARE(MarkdownRuntime::plainHeadingText(QStringLiteral("<span>x</span>")),
             QStringLiteral("x"));
    QCOMPARE(MarkdownRuntime::plainHeadingText(QStringLiteral("###  spaced ##")),
             QStringLiteral("spaced"));
    // 转义的星号是文本本身
    QCOMPARE(MarkdownRuntime::plainHeadingText(QStringLiteral("a \\* b")),
             QStringLiteral("a * b"));
}

/**
 * @brief 页面文案全 ASCII、非空，且键位够用
 *
 * JS 侧一个可翻译串都不写（lupdate 看不见 .js），所以这份字典里少一个键，
 * 页面上就少一句能翻译的话——键名清单在这里钉住。
 */
void TestMarkdownRuntime::testPageStringsAreAsciiAndComplete()
{
    const QJsonObject strings = MarkdownRuntime::pageStrings();
    QVERIFY(strings.size() >= 28);
    for (const QString &key : strings.keys()) {
        const QString text = strings.value(key).toString();
        QVERIFY2(!text.isEmpty(),
                 qPrintable(QStringLiteral("%1 has no text").arg(key)));
        // 源语言是英文、且必须纯 ASCII。门禁规则 3 只扫 tr()/qsTr()，
        // 而这份字典走的是带上下文的 translate()——它得自己把关，否则
        // 「顺手把分隔符写成 ·」这种改动会一路进 .ts，翻译侧无从分辨。
        QVERIFY2(isAsciiText(text),
                 qPrintable(QStringLiteral("%1 is not ASCII: %2")
                                .arg(key, text)));
    }
    const QStringList needed = {
        QStringLiteral("outline"),     QStringLiteral("search"),
        QStringLiteral("findPlaceholder"), QStringLiteral("noMatches"),
        QStringLiteral("matchCount"),  QStringLiteral("caseSensitive"),
        QStringLiteral("previous"),    QStringLiteral("next"),
        QStringLiteral("closeSearch"), QStringLiteral("copy"),
        QStringLiteral("copySource"),  QStringLiteral("openExternal"),
        QStringLiteral("exportHtml"),  QStringLiteral("exported"),
        QStringLiteral("save"),        QStringLiteral("saved"),
        QStringLiteral("discard"),     QStringLiteral("reload"),
        QStringLiteral("edit"),        QStringLiteral("previewOnly"),
        QStringLiteral("fontSize"),    QStringLiteral("contentWidth"),
        QStringLiteral("renderError"), QStringLiteral("diagramError"),
        QStringLiteral("diagramDeferred"), QStringLiteral("emptyDocument"),
        QStringLiteral("changedOnDisk"), QStringLiteral("stats"),
    };
    for (const QString &key : needed) {
        QVERIFY2(strings.contains(key),
                 qPrintable(QStringLiteral("page strings lack '%1'").arg(key)));
    }
}

/**
 * @brief 主题 → CSS 变量：无主题时只剩变体名，有主题时颜色与尺寸齐备
 */
void TestMarkdownRuntime::testCssVariableKeys()
{
    const QVariantMap empty = MarkdownRuntime::cssVariables(nullptr);
    QCOMPARE(empty.size(), 1);
    QCOMPARE(empty.value(QStringLiteral("--awb-variant")).toString(),
             QStringLiteral("dark"));
}

#include "tst_markdownruntime.moc"
