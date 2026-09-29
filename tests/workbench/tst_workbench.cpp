#include "awbtest.h"

#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentRepository.h"
#include "agentcatalog/AgentsFacade.h"
#include "core/Settings.h"
#include "shell/NavigationModel.h"
#include "shell/Notifications.h"
#include "shell/UiServices.h"
#include "web/WebTabsFacade.h"
#include "web/WebTabsModel.h"
#include "workbench/WorkbenchContext.h"

#include <QDesktopServices>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using awb::agentcatalog::AgentRepository;
using awb::agentcatalog::AgentsFacade;
using awb::core::Settings;
using awb::shell::NavigationModel;
using awb::shell::Notifications;
using awb::shell::PageDescriptor;
using awb::shell::UiServices;
using awb::web::WebTabsFacade;
using awb::workbench::WorkbenchContext;

namespace {

// Captures the URL a "browser open" would have handled, so the test never
// launches a real browser (QDesktopServices routes to the installed
// handler instead of the OS).
class UrlRecorder : public QObject
{
    Q_OBJECT
public:
    QUrl lastUrl;
public Q_SLOTS:
    void record(const QUrl &url) { lastUrl = url; }
};

} // namespace

// The pages WorkbenchContext navigates between ("agents" must be
// registered too — NavigationModel rejects unknown ids).
void registerNavPages(NavigationModel *nav)
{
    PageDescriptor agentsPage;
    agentsPage.id = QStringLiteral("agents");
    QVERIFY2(nav->registerPage(agentsPage), "agents page");
    PageDescriptor webPage;
    webPage.id = QStringLiteral("web");
    QVERIFY2(nav->registerPage(webPage), "web page");
}

class TestWorkbenchContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // openWeb creates the tab AND takes the user to the web page; the
    // external-surface policy opens no tab and must not navigate either.
    void testOpenWebNavigates()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        Settings settings;
        AgentsFacade agents(&settings, tmp.path());
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        NavigationModel nav;
        Notifications notifications;
        UiServices ui;
        WorkbenchContext workbench(&nav, &ui, &notifications, &agents,
                                   &web, &settings);

        registerNavPages(&nav);
        nav.setCurrentPageId(QStringLiteral("agents"));

        // The agent must exist (the bundled defaults seed the model).
        const int row = agents.agentModel()->indexOf(
            QStringLiteral("kimi-code"));
        QVERIFY(row >= 0);

        workbench.openWeb(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(nav.currentPageId(), QStringLiteral("web"));

        // Leaving the page and opening again activates the existing tab
        // (no duplicate) and navigates back.
        nav.setCurrentPageId(QStringLiteral("agents"));
        workbench.openWeb(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(nav.currentPageId(), QStringLiteral("web"));

        // Unknown agent: no tab, no navigation, no crash.
        nav.setCurrentPageId(QStringLiteral("agents"));
        workbench.openWeb(QStringLiteral("no-such-agent"));
        QCOMPARE(web.tabs()->rowCount(), 1);
        QCOMPARE(nav.currentPageId(), QStringLiteral("agents"));
    }

    // With the external surface policy openWeb hands the URL to the
    // browser: no tab is created and the web page is not switched to.
    void testOpenWebExternalSurfaceDoesNotNavigate()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        Settings settings;
        settings.setWebSurface(QStringLiteral("external"));
        AgentsFacade agents(&settings, tmp.path());
        WebTabsFacade web(&settings);
        NavigationModel nav;
        Notifications notifications;
        UiServices ui;
        WorkbenchContext workbench(&nav, &ui, &notifications, &agents,
                                   &web, &settings);

        registerNavPages(&nav);
        nav.setCurrentPageId(QStringLiteral("agents"));

        workbench.openWeb(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 0);
        QCOMPARE(nav.currentPageId(), QStringLiteral("agents"));
    }

    // The card's split-button alternative: no tab, no navigation, the
    // final URL (token fragment included) handed to the system browser.
    void testOpenWebExternalHandsUrlToBrowser()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AgentRepository seed(tmp.path());
            seed.load();
        }
        Settings settings;
        AgentsFacade agents(&settings, tmp.path());
        WebTabsFacade web(&settings);
        web.registerSurface(QStringLiteral("embedded"),
                            QStringLiteral("qrc:/fake/Surface.qml"));
        NavigationModel nav;
        Notifications notifications;
        UiServices ui;
        WorkbenchContext workbench(&nav, &ui, &notifications, &agents,
                                   &web, &settings);

        registerNavPages(&nav);
        nav.setCurrentPageId(QStringLiteral("agents"));

        UrlRecorder recorder;
        QDesktopServices::setUrlHandler(QStringLiteral("http"), &recorder,
                                        "record");

        workbench.openWebExternal(QStringLiteral("kimi-code"));
        QCOMPARE(web.tabs()->rowCount(), 0);
        QCOMPARE(nav.currentPageId(), QStringLiteral("agents"));
        QVERIFY(recorder.lastUrl.isValid());
        QCOMPARE(recorder.lastUrl.host(), QStringLiteral("127.0.0.1"));

        // Unknown agent: nothing handed anywhere, no crash.
        recorder.lastUrl.clear();
        workbench.openWebExternal(QStringLiteral("no-such-agent"));
        QVERIFY(!recorder.lastUrl.isValid());

        QDesktopServices::unsetUrlHandler(QStringLiteral("http"));
    }
};

AWB_TEST(TestWorkbenchContext)
#include "tst_workbench.moc"

// Entry point of the workbench test suite (single class today, registered
// through awbtest.h like the other multi-class suites).
int main(int argc, char *argv[])
{
    return awbRunRegisteredTests(argc, argv);
}
