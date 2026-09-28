#ifndef AWB_SKILLS_SKILLSFACADE_H
#define AWB_SKILLS_SKILLSFACADE_H

#include "core/OpResult.h"
#include "skillcatalog/SkillScanner.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::skillcatalog {

class SkillModel;

// The QML facade for the skills feature.
class SkillsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // Roots for the settings UI: [{id,label,path,kind,enabled}]. A property
    // (not only the roots() invokable) so the list re-binds after
    // addRoot/removeRoot/setRootEnabled — a bare method call never did.
    Q_PROPERTY(QVariantList roots READ roots NOTIFY rootsChanged)
    // "%n skill(s) found" style stats for the page footer.
    Q_PROPERTY(QString statsText READ statsText NOTIFY statsChanged)
    // True when the last scan had unreadable/missing roots.
    Q_PROPERTY(bool partialFailure READ partialFailure NOTIFY statsChanged)

public:
    SkillsFacade(core::Settings *settings, QObject *parent = nullptr);

    QAbstractItemModel *model() const;
    SkillModel *skillModel() const { return m_model; }

    bool scanning() const { return m_scanning; }
    QString statsText() const;
    bool partialFailure() const;

    // Rescan; scanStarted/scanFinished bracket it (sync today, async-ready
    // interface.
    Q_INVOKABLE void refresh();

    // Roots for the settings UI (Q_PROPERTY READ — see above).
    QVariantList roots() const;
    Q_INVOKABLE void setRootEnabled(const QString &id, bool enabled);
    // Add a custom root (path from the settings UI).
    Q_INVOKABLE bool addRoot(const QString &path);
    Q_INVOKABLE bool removeRoot(const QString &id);

    // 返回类型必须写全限定名：Qt 5 的 moc 按头文件书写形式记录返回类型名，
    // QML 调用端按 QMetaType 注册名（即类全名 awb::core::OpResult）解析；
    // 短名解析不到注册表就抛 "Unknown method return type"，调用静默失效。
    // Copy the skill directory path to the clipboard; the caller shows the
    // toast from the OpResult.
    Q_INVOKABLE awb::core::OpResult copyPath(const QString &skillFilePath);
    Q_INVOKABLE awb::core::OpResult copySkillFile(const QString &skillFilePath);
    Q_INVOKABLE awb::core::OpResult copyName(const QString &skillFilePath);

    Q_INVOKABLE awb::core::OpResult openFolder(const QString &skillFilePath);
    Q_INVOKABLE awb::core::OpResult revealSkillFile(const QString &skillFilePath);

    // The definition behind a SKILL.md path (for the hover flyout).
    Q_INVOKABLE QVariantMap skill(const QString &skillFilePath) const;

    // Display label for a skill root kind ("agents" -> "Agents", …).
    // Single source for the Settings and Skills pages — the literal
    // switch was previously duplicated in QML, invisible to lupdate
    // maintenance.
    Q_INVOKABLE QString kindLabel(const QString &kind) const;

signals:
    void scanningChanged();
    void statsChanged();
    void rootsChanged();
    void scanStarted();
    void scanFinished();

private:
    const SkillDefinition *find(const QString &skillFilePath) const;
    static QString parentDir(const QString &skillFilePath);
    // Shared tail of copyPath/copySkillFile/copyName.
    core::OpResult copyToClipboard(const QString &text);

    core::Settings *m_settings;
    SkillModel *m_model;
    SkillScanner *m_scanner;
    bool m_scanning = false;
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLSFACADE_H
