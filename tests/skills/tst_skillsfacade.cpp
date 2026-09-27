#include "awbtest.h"

#include "core/Settings.h"
#include "skills/SkillsFacade.h"

#include <QStandardPaths>
#include <QtTest>

using awb::core::Settings;
using awb::skills::SkillsFacade;

// kindLabel(): the shared display mapping for skill root kinds (single
// source for the Settings and Skills pages).
class TestSkillsFacade : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void testKindLabel()
    {
        Settings settings;
        SkillsFacade facade(&settings);

        QCOMPARE(facade.kindLabel(QStringLiteral("agents")),
                 QStringLiteral("Agents"));
        QCOMPARE(facade.kindLabel(QStringLiteral("claude")),
                 QStringLiteral("Claude"));
        QCOMPARE(facade.kindLabel(QStringLiteral("codex")),
                 QStringLiteral("Codex"));
        QCOMPARE(facade.kindLabel(QStringLiteral("plugin")),
                 QStringLiteral("Plugin"));
        QCOMPARE(facade.kindLabel(QStringLiteral("project")),
                 QStringLiteral("Project"));
        QCOMPARE(facade.kindLabel(QStringLiteral("custom")),
                 QStringLiteral("Custom"));
        // Unknown kinds pass through unchanged.
        QCOMPARE(facade.kindLabel(QStringLiteral("future-kind")),
                 QStringLiteral("future-kind"));
        QCOMPARE(facade.kindLabel(QString()), QString());
    }
};

AWB_TEST(TestSkillsFacade)
#include "tst_skillsfacade.moc"
