#include "shell/ShellController.h"

#include "core/Settings.h"

namespace awb::shell {

ShellController::ShellController(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // External edits to settings.json keep the window in sync.
    connect(m_settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QLatin1String("window.sidebarCollapsed"))
                    emit sidebarCollapsedChanged();
                else if (key == QLatin1String("window.sidebarWidth"))
                    emit sidebarWidthChanged();
                else if (key == QLatin1String("window.width")
                         || key == QLatin1String("window.height"))
                    emit windowSizeChanged();
                else if (key == QLatin1String("window.title"))
                    emit windowTitleChanged();
            });
}

QString ShellController::windowTitle() const
{
    return m_settings->windowTitle();
}

bool ShellController::sidebarCollapsed() const
{
    return m_settings->window().sidebarCollapsed;
}

void ShellController::setSidebarCollapsed(bool collapsed)
{
    if (m_settings->window().sidebarCollapsed == collapsed)
        return;
    m_settings->setSidebarCollapsed(collapsed);
    m_settings->save();
    emit sidebarCollapsedChanged();
}

int ShellController::sidebarWidth() const
{
    return m_settings->window().sidebarWidth;
}

int ShellController::windowWidth() const
{
    return m_settings->window().width;
}

int ShellController::windowHeight() const
{
    return m_settings->window().height;
}

void ShellController::saveWindowSize(int width, int height)
{
    if (m_settings->window().width == width
        && m_settings->window().height == height)
        return;
    m_settings->setWindowSize(width, height);
    m_settings->save();
    emit windowSizeChanged();
}

QString ShellController::lastPageId() const
{
    return m_settings->window().lastPageId;
}

void ShellController::setLastPageId(const QString &id)
{
    if (m_settings->window().lastPageId == id)
        return;
    m_settings->setLastPageId(id);
    m_settings->save();
}

} // namespace awb::shell
