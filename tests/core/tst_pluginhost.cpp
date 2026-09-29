#include "awbtest.h"

#include "core/Paths.h"
#include "core/PluginHost.h"
#include "plugin_api/PluginApi.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using awb::core::Paths;
using awb::core::PluginHost;

namespace {

/// 最小化的空实现 Services：两个加载用例只走到「任何插件代码可能回调宿主
/// 之前」的策略检查，用不到真正的服务逻辑。
class DummyServices : public awb::plugin::Services
{
public:
    void registerPage(const awb::plugin::PageDescriptor &) override {}
    void unregisterPage(const QString &) override {}
    void addWebSurface(const QString &, const QString &) override {}
    QString dataDir(const QString &) override { return {}; }
    void log(int, const QString &) override {}
    void notify(int, const QString &, const QString &) override {}
    QString themeColor(const QString &) override { return {}; }
    QString settingsValue(const QString &) override { return {}; }
};

} // namespace

/// 测 core::PluginHost 的插件发现与加载策略：manifest 只解析不加载、坏
/// manifest 只隐藏自己、API 版本不符拒绝加载、用户未启用前什么都不加载。
class TestPluginHost : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        Paths::setDataRootForTesting(QString());
    }

    void cleanup()
    {
        Paths::setDataRootForTesting(QString());
    }

    // discover() 读取 <dataRoot>/plugins/*/plugin.json 并映射 manifest 的
    // 每个字段——全程不加载任何库。
    void testDiscoverParsesManifest()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());
        const QString dir = tmp.path() + QStringLiteral("/plugins/hello");
        QVERIFY(QDir().mkpath(dir));

        QFile f(dir + QStringLiteral("/plugin.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(R"({
            "id": "hello",
            "name": "Hello",
            "version": "0.1.0",
            "apiVersion": 1,
            "description": "The smallest example plugin.",
            "author": "AgentWorkbench",
            "entry": "hello.dll",
            "pages": [
                { "id": "hello", "title": "Hello",
                  "icon": "qrc:/icons/bot.svg",
                  "source": "qrc:/hello/HelloPage.qml",
                  "section": "extensions", "order": 50 }
            ]
        })");
        f.close();

        const PluginHost host;
        const QList<PluginHost::Manifest> found = host.discover();
        QCOMPARE(found.size(), 1);
        const PluginHost::Manifest &m = found.first();
        QCOMPARE(m.id, QStringLiteral("hello"));
        QCOMPARE(m.name, QStringLiteral("Hello"));
        QCOMPARE(m.version, QStringLiteral("0.1.0"));
        QCOMPARE(m.apiVersion, 1);
        QCOMPARE(m.entry, QStringLiteral("hello.dll"));
        QCOMPARE(m.dir, dir);
        QCOMPARE(m.pages.size(), 1);
        QCOMPARE(m.pages.first().id, QStringLiteral("hello"));
        // discover() 从不判定启用状态——那是 loader 的职责。
        QVERIFY(!m.enabled);
    }

    // 坏 manifest 与缺 id/entry 的被跳过，合法的邻居照常出现：
    // 一个坏插件不能连累其它插件。
    void testBrokenManifestsAreSkipped()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());
        const QString root = tmp.path() + QStringLiteral("/plugins");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/bad-json")));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/no-id")));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/good")));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/bare")));

        auto write_file = [](const QString &path, const QByteArray &bytes) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return false;
            }
            return f.write(bytes) == bytes.size();
        };
        QVERIFY(write_file(root + QStringLiteral("/bad-json/plugin.json"),
                           QByteArrayLiteral("{ this is not json")));
        QVERIFY(write_file(root + QStringLiteral("/no-id/plugin.json"),
                           QByteArrayLiteral(R"({"name":"x","entry":""})")));
        QVERIFY(write_file(root + QStringLiteral("/good/plugin.json"),
                           QByteArrayLiteral(
                               R"({"id":"good","entry":"good.dll",
                               "apiVersion":1})")));

        const PluginHost host;
        const QList<PluginHost::Manifest> found = host.discover();
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().id, QStringLiteral("good"));
    }

    // 目标其它 API 版本的 manifest 在触碰任何库之前就被拒绝
    // （版本不符 → 记日志 + 跳过）。
    void testApiVersionMismatchIsRefused()
    {
        PluginHost::Manifest manifest;
        manifest.id = QStringLiteral("future");
        manifest.entry = QStringLiteral("future.dll");
        manifest.apiVersion = awb::plugin::ApiVersion + 1;
        manifest.enabled = true;

        DummyServices services;
        PluginHost host;
        // 版本检查发生在任何加载之前，所以磁盘上的入口文件在这里无关紧要。
        QCOMPARE(host.loadEnabled({manifest}, &services), 0);
    }

    // 发现的插件初始为禁用：用户启用之前 loadEnabled() 不得碰它（默认禁用）。
    void testDisabledPluginsAreNotLoaded()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Paths::setDataRootForTesting(tmp.path());
        const QString dir = tmp.path() + QStringLiteral("/plugins/off");
        QVERIFY(QDir().mkpath(dir));
        QFile f(dir + QStringLiteral("/plugin.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(R"({"id":"off","entry":"off.dll","apiVersion":1})");
        f.close();

        const PluginHost host;
        const QList<PluginHost::Manifest> found = host.discover();
        QCOMPARE(found.size(), 1);
        QVERIFY(!found.first().enabled);

        // enabled=false → 直接跳过、不尝试加载；入口文件故意不存在，
        // 一旦发生加载就会在日志里现形。
        DummyServices services;
        PluginHost loader;
        QCOMPARE(loader.loadEnabled(found, &services), 0);
    }
};

AWB_TEST(TestPluginHost)

#include "tst_pluginhost.moc"
