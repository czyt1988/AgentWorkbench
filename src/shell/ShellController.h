#ifndef AWB_SHELL_SHELLCONTROLLER_H
#define AWB_SHELL_SHELLCONTROLLER_H

#include <QObject>
#include <QString>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::shell {

// Window-level state — every value is persisted in settings.json through
// core::Settings (01-architecture.md §4.7): sidebar collapse/width, window
// size, the last visited page. Restarting restores the previous session's
// shape (03-migration-plan.md S4-T9).
class ShellController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(bool sidebarCollapsed READ sidebarCollapsed WRITE
                   setSidebarCollapsed NOTIFY sidebarCollapsedChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY sidebarWidthChanged)
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY windowSizeChanged)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY windowSizeChanged)

public:
    explicit ShellController(core::Settings *settings,
                             QObject *parent = nullptr);

    // settings.json window.title; empty = the application default.
    QString windowTitle() const;

    bool sidebarCollapsed() const;
    void setSidebarCollapsed(bool collapsed);
    int sidebarWidth() const;

    int windowWidth() const;
    int windowHeight() const;
    // Persist the current window geometry (called by MainWindow on close).
    Q_INVOKABLE void saveWindowSize(int width, int height);

    // Last visited page, restored at startup and saved on every switch.
    QString lastPageId() const;
    void setLastPageId(const QString &id);

signals:
    void windowTitleChanged();
    void sidebarCollapsedChanged();
    void sidebarWidthChanged();
    void windowSizeChanged();

private:
    core::Settings *m_settings;
};

} // namespace awb::shell

#endif // AWB_SHELL_SHELLCONTROLLER_H
