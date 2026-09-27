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

// Minimal no-op Services — the two load tests only exercise policy checks
// that run before any plugin code could call into the host.
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

// Plugin discovery and load policy: manifests are
// parsed without loading anything, a broken manifest only hides itself,
// an API-version mismatch is refused, and nothing loads until the user
// opts in.
class TestPluginHost : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        Paths::setDataRootForTesting(QString());
    }

    void cleanup()
    {
        Paths::setDataRootForTesting(QString());
    }

    // discover() reads <dataRoot>/plugins/*/plugin.json and maps every
    // manifest field — without ever loading a library.
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
        // discover() never resolves enablement — that's the loader's job.
        QVERIFY(!m.enabled);
    }

    // A malformed manifest and one missing id/entry are skipped, the valid
    // sibling still comes through: one bad plugin never hides the others.
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
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;
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

    // A manifest targeting another API version is refused before any
    // library is touched (mismatch → log + skip).
    void testApiVersionMismatchIsRefused()
    {
        PluginHost::Manifest manifest;
        manifest.id = QStringLiteral("future");
        manifest.entry = QStringLiteral("future.dll");
        manifest.apiVersion = awb::plugin::ApiVersion + 1;
        manifest.enabled = true;

        DummyServices services;
        PluginHost host;
        // The mismatch check runs before any load, so the entry file on
        // disk never matters here.
        QCOMPARE(host.loadEnabled({manifest}, &services), 0);
    }

    // A discovered plugin starts disabled: loadEnabled() must not touch it
    // until the user opts in (disabled by default).
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

        // enabled=false → skipped without attempting a load; the entry
        // file intentionally does not exist, so an attempted load would
        // be visible in the log.
        DummyServices services;
        PluginHost loader;
        QCOMPARE(loader.loadEnabled(found, &services), 0);
    }
};

AWB_TEST(TestPluginHost)

#include "tst_pluginhost.moc"
