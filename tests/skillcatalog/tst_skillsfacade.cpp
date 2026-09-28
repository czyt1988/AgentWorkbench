#include "awbtest.h"

#include "core/Settings.h"
#include "skillcatalog/SkillsFacade.h"

#include <QStandardPaths>
#include <QtTest>

using awb::core::Settings;
using awb::skillcatalog::SkillsFacade;

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
    // QML 调用端按 moc 记录的类型名查 QMetaType 注册表（守卫的完整说明
    // 见 awbUnresolvedQmlCallTypes——SkillCard 直接调用这些方法，短名声明
    // 会让它们全部静默失效）；注册序与 app/main.cpp 一致。
    void testQmlMethodTypesResolve()
    {
        qRegisterMetaType<awb::core::OpResult>();
        Settings settings;
        SkillsFacade facade(&settings);
        const QStringList failures = awbUnresolvedQmlCallTypes(&facade);
        QVERIFY2(failures.isEmpty(),
                 qPrintable(failures.join(QLatin1String("\n"))));
    }
};

AWB_TEST(TestSkillsFacade)
#include "tst_skillsfacade.moc"
