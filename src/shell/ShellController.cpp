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
                if (key == QStringLiteral("window.sidebarCollapsed")) {
                    Q_EMIT sidebarCollapsedChanged();
                }
                else if (key == QStringLiteral("window.sidebarWidth")) {
                    Q_EMIT sidebarWidthChanged();
                }
                else if (key == QStringLiteral("window.width")
                         || key == QStringLiteral("window.height")) {
                    Q_EMIT windowSizeChanged();
                }
                else if (key == QStringLiteral("window.title")) {
                    Q_EMIT windowTitleChanged();
                }
                else if (key == QStringLiteral("web.surface")) {
                    Q_EMIT webSurfaceChanged();
                }
                else if (key == QStringLiteral("web.chromiumFlags")) {
                    Q_EMIT webChromiumFlagsChanged();
                }
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
    if (m_settings->window().sidebarCollapsed == collapsed) {
        return;
    }
    m_settings->setSidebarCollapsed(collapsed);
    m_settings->save();
    Q_EMIT sidebarCollapsedChanged();
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
        && m_settings->window().height == height) {
        return;
    }
    m_settings->setWindowSize(width, height);
    m_settings->save();
    Q_EMIT windowSizeChanged();
}

QString ShellController::webSurface() const
{
    return m_settings->webOptions().surface;
}

void ShellController::setWebSurface(const QString &surface)
{
    if (m_settings->webOptions().surface == surface) {
        return;
    }
    m_settings->setWebSurface(surface);
    m_settings->save();
    Q_EMIT webSurfaceChanged();
}

QString ShellController::webChromiumFlags() const
{
    return m_settings->webOptions().chromiumFlags;
}

void ShellController::setWebChromiumFlags(const QString &flags)
{
    if (m_settings->webOptions().chromiumFlags == flags) {
        return;
    }
    m_settings->setWebChromiumFlags(flags);
    m_settings->save();
    Q_EMIT webChromiumFlagsChanged();
}

QString ShellController::lastPageId() const
{
    return m_settings->window().lastPageId;
}

void ShellController::setLastPageId(const QString &id)
{
    if (m_settings->window().lastPageId == id) {
        return;
    }
    m_settings->setLastPageId(id);
    m_settings->save();
}

} // namespace awb::shell
