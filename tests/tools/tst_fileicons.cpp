#include "awbtest.h"

#include "tools/FileIcons.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using awb::tools::FileIcons;

/// 测 tools::FileIcons 的图标映射表：内置默认表随测试目标打包（见
/// CMakeLists.txt），用户表叠加覆盖；覆盖文件名优先于后缀、未知名字回退
/// 默认图标，以及坏值不变成空白。
class TestFileIcons : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testBuiltinSuffixMapping()
    {
        FileIcons icons;
        QCOMPARE(icons.forFile(QStringLiteral("README.md")),
                 QStringLiteral("qrc:/icons/filetypes/markdown.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("main.cpp")),
                 QStringLiteral("qrc:/icons/filetypes/cfamily.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("app.tsx")),
                 QStringLiteral("qrc:/icons/filetypes/typescript.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("build.sh")),
                 QStringLiteral("qrc:/icons/filetypes/shell.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("logo.png")),
                 QStringLiteral("qrc:/icons/filetypes/image.svg"));
        // 后缀大小写不敏感（磁盘上写的是 .MD 也一样）。
        QCOMPARE(icons.forFile(QStringLiteral("NOTES.MD")),
                 icons.forFile(QStringLiteral("notes.md")));
    }

    void testFileNameBeatsSuffix()
    {
        FileIcons icons;
        // CMakeLists.txt 的后缀是 txt，但要走 cmake 图标。
        QCOMPARE(icons.forFile(QStringLiteral("CMakeLists.txt")),
                 QStringLiteral("qrc:/icons/filetypes/cmake.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("cmakelists.txt")),
                 QStringLiteral("qrc:/icons/filetypes/cmake.svg"));
        // 没有后缀的名字（README、.gitignore）只能靠文件名匹配。
        QCOMPARE(icons.forFile(QStringLiteral("README")),
                 QStringLiteral("qrc:/icons/filetypes/markdown.svg"));
        QCOMPARE(icons.forFile(QStringLiteral(".gitignore")),
                 QStringLiteral("qrc:/icons/filetypes/config.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("LICENSE")),
                 QStringLiteral("qrc:/icons/filetypes/text.svg"));
    }

    void testUnknownFileFallsBackToDefault()
    {
        FileIcons icons;
        QCOMPARE(icons.forFile(QStringLiteral("mystery.qqq")),
                 QStringLiteral("qrc:/icons/file.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("noextension")),
                 QStringLiteral("qrc:/icons/file.svg"));
        QCOMPARE(icons.forFile(QString()), QStringLiteral("qrc:/icons/file.svg"));
    }

    void testFolderMapping()
    {
        FileIcons icons;
        QCOMPARE(icons.forFolder(QStringLiteral("docs")),
                 QStringLiteral("qrc:/icons/foldertypes/docs.svg"));
        // 目录名同样大小写不敏感。
        QCOMPARE(icons.forFolder(QStringLiteral("SRC")),
                 QStringLiteral("qrc:/icons/foldertypes/src.svg"));
        QCOMPARE(icons.forFolder(QStringLiteral("node_modules")),
                 QStringLiteral("qrc:/icons/foldertypes/deps.svg"));
        QCOMPARE(icons.forFolder(QStringLiteral("whatever")),
                 QStringLiteral("qrc:/icons/folder.svg"));
        QCOMPARE(icons.forFolder(QString()), QStringLiteral("qrc:/icons/folder.svg"));
    }

    void testUserFileOverridesAndExtends()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeUserConfig(dir.path(), QJsonObject{
            {QStringLiteral("defaults"), QJsonObject{
                 {QStringLiteral("file"),
                  QStringLiteral("qrc:/icons/filetypes/text.svg")}}},
            {QStringLiteral("suffixes"), QJsonObject{
                 // 覆盖内置的 md 映射，并新增一个内置表没有的后缀。
                 {QStringLiteral("md"),
                  QStringLiteral("qrc:/icons/filetypes/qml.svg")},
                 {QStringLiteral("zzz"),
                  QStringLiteral("qrc:/icons/filetypes/pdf.svg")}}},
            {QStringLiteral("folderNames"), QJsonObject{
                 {QStringLiteral("whatever"),
                  QStringLiteral("qrc:/icons/foldertypes/docs.svg")}}},
        });

        FileIcons icons;
        icons.loadUserFile(path);

        QCOMPARE(icons.forFile(QStringLiteral("notes.md")),
                 QStringLiteral("qrc:/icons/filetypes/qml.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("data.zzz")),
                 QStringLiteral("qrc:/icons/filetypes/pdf.svg"));
        QCOMPARE(icons.forFolder(QStringLiteral("whatever")),
                 QStringLiteral("qrc:/icons/foldertypes/docs.svg"));
        // 没被用户表提到的键保持内置值；默认图标换成用户给的那张。
        QCOMPARE(icons.forFile(QStringLiteral("main.cpp")),
                 QStringLiteral("qrc:/icons/filetypes/cfamily.svg"));
        QCOMPARE(icons.forFile(QStringLiteral("mystery.qqq")),
                 QStringLiteral("qrc:/icons/filetypes/text.svg"));
    }

    void testUnusableValueFallsBackInsteadOfGoingBlank()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // 相对路径且文件不存在：IconResolver 认不出来，退回默认图标。
        const QString path = writeUserConfig(dir.path(), QJsonObject{
            {QStringLiteral("suffixes"), QJsonObject{
                 {QStringLiteral("zzz"), QStringLiteral("no-such-icon.svg")}}},
        });

        FileIcons icons;
        icons.loadUserFile(path);
        QCOMPARE(icons.forFile(QStringLiteral("data.zzz")),
                 QStringLiteral("qrc:/icons/file.svg"));
    }

    void testMissingUserFileIsIgnored()
    {
        FileIcons icons;
        icons.loadUserFile(QStringLiteral("/no/such/dir/file_icons.json"));
        icons.loadUserFile(QString());
        QCOMPARE(icons.forFile(QStringLiteral("notes.md")),
                 QStringLiteral("qrc:/icons/filetypes/markdown.svg"));
    }

private:
    /// 把一段用户配置写进 dir，返回它的路径（打不开时返回空串）。
    static QString writeUserConfig(const QString &dir, const QJsonObject &root)
    {
        const QString path = QDir(dir).filePath(QStringLiteral("file_icons.json"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return QString();
        }
        file.write(QJsonDocument(root).toJson());
        return path;
    }
};

AWB_TEST(TestFileIcons)
#include "tst_fileicons.moc"
