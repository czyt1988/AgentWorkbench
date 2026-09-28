#ifndef AWB_THEME_THEME_H
#define AWB_THEME_THEME_H

#include "theme/ThemeFile.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::theme {

class ThemeRegistry;

// The QML global `theme`: semantic tokens as
// named, NOTifiable properties (names are the contract), plus dynamic
// helpers. Every token updates
// together through the single `changed` signal, so switching a theme
// re-binds the whole UI at runtime.
class Theme : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString variant READ variant NOTIFY changed)
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    Q_PROPERTY(QVariantList availableThemes READ availableThemes NOTIFY changed)
    Q_PROPERTY(QStringList agentPalette READ agentPalette NOTIFY changed)

    Q_PROPERTY(QColor windowBg READ windowBg NOTIFY changed)
    Q_PROPERTY(QColor sidebarBg READ sidebarBg NOTIFY changed)
    Q_PROPERTY(QColor workspaceBg READ workspaceBg NOTIFY changed)
    Q_PROPERTY(QColor surfaceBg READ surfaceBg NOTIFY changed)
    Q_PROPERTY(QColor surfaceAltBg READ surfaceAltBg NOTIFY changed)
    Q_PROPERTY(QColor surfaceHoverBg READ surfaceHoverBg NOTIFY changed)
    Q_PROPERTY(QColor chromeBg READ chromeBg NOTIFY changed)
    Q_PROPERTY(QColor overlayBg READ overlayBg NOTIFY changed)
    Q_PROPERTY(QColor consoleBg READ consoleBg NOTIFY changed)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY changed)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY changed)
    Q_PROPERTY(QColor textOnAccent READ textOnAccent NOTIFY changed)
    Q_PROPERTY(QColor textLink READ textLink NOTIFY changed)
    Q_PROPERTY(QColor borderSubtle READ borderSubtle NOTIFY changed)
    Q_PROPERTY(QColor borderStrong READ borderStrong NOTIFY changed)
    Q_PROPERTY(QColor separator READ separator NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor focusRing READ focusRing NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor info READ info NOTIFY changed)
    Q_PROPERTY(QColor neutralOff READ neutralOff NOTIFY changed)
    Q_PROPERTY(QColor tooltipBg READ tooltipBg NOTIFY changed)
    Q_PROPERTY(QColor tooltipText READ tooltipText NOTIFY changed)
    Q_PROPERTY(QColor badgeBg READ badgeBg NOTIFY changed)
    Q_PROPERTY(QColor tabActiveBg READ tabActiveBg NOTIFY changed)
    Q_PROPERTY(QColor tabInactiveBg READ tabInactiveBg NOTIFY changed)
    Q_PROPERTY(QColor selectionBg READ selectionBg NOTIFY changed)
    Q_PROPERTY(QColor scrollbar READ scrollbar NOTIFY changed)
    Q_PROPERTY(double radiusCard READ radiusCard NOTIFY changed)
    Q_PROPERTY(double radiusOverlay READ radiusOverlay NOTIFY changed)
    Q_PROPERTY(double radiusControl READ radiusControl NOTIFY changed)
    Q_PROPERTY(double radiusPill READ radiusPill NOTIFY changed)
    Q_PROPERTY(double spacingXs READ spacingXs NOTIFY changed)
    Q_PROPERTY(double spacingS READ spacingS NOTIFY changed)
    Q_PROPERTY(double spacingM READ spacingM NOTIFY changed)
    Q_PROPERTY(double spacingL READ spacingL NOTIFY changed)
    Q_PROPERTY(double spacingXl READ spacingXl NOTIFY changed)
    Q_PROPERTY(double fontSizeCaption READ fontSizeCaption NOTIFY changed)
    Q_PROPERTY(double fontSizeSmall READ fontSizeSmall NOTIFY changed)
    Q_PROPERTY(double fontSizeBody READ fontSizeBody NOTIFY changed)
    Q_PROPERTY(double fontSizeSubtitle READ fontSizeSubtitle NOTIFY changed)
    Q_PROPERTY(double fontSizeCardTitle READ fontSizeCardTitle NOTIFY changed)
    Q_PROPERTY(double fontSizePageTitle READ fontSizePageTitle NOTIFY changed)
    Q_PROPERTY(double cardMinWidth READ cardMinWidth NOTIFY changed)
    Q_PROPERTY(double cardHeight READ cardHeight NOTIFY changed)
    Q_PROPERTY(double durationFast READ durationFast NOTIFY changed)
    Q_PROPERTY(double durationNormal READ durationNormal NOTIFY changed)
    Q_PROPERTY(double sidebarWidth READ sidebarWidth NOTIFY changed)
    Q_PROPERTY(double sidebarCollapsedWidth READ sidebarCollapsedWidth NOTIFY changed)
    Q_PROPERTY(double statusBarHeight READ statusBarHeight NOTIFY changed)
    Q_PROPERTY(double tabBarHeight READ tabBarHeight NOTIFY changed)
    Q_PROPERTY(double toastWidth READ toastWidth NOTIFY changed)
    Q_PROPERTY(QString family READ family NOTIFY changed)
    Q_PROPERTY(QString monoFamily READ monoFamily NOTIFY changed)
    // 本机可用字体族（外观页的字体选择框用；进程内静态）。
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)

public:
    Theme(core::Settings *settings, ThemeRegistry *registry,
          QObject *parent = nullptr);

    QString variant() const;
    QString themeId() const;
    QVariantList availableThemes() const;
    QStringList agentPalette() const;

    // Switch the theme at runtime: writes appearance.theme to settings.json
    // and re-binds every token.
    Q_INVOKABLE void applyTheme(const QString &id);

    // Switch the global UI font at runtime: writes appearance.fontFamily
    // (empty = follow the theme / system default) and re-binds family.
    Q_INVOKABLE void setFontFamily(const QString &family);

    // Dynamic token lookup (for components that iterate tokens).
    Q_INVOKABLE QColor color(const QString &name) const;
    Q_INVOKABLE double metric(const QString &name) const;

    // Derived colors: alpha overlay, hover/press shading whose
    // direction depends on the variant. QML must use these instead of
    // Qt.darker/Qt.lighter.
    Q_INVOKABLE QColor alpha(const QColor &color, qreal a) const;
    Q_INVOKABLE QColor hover(const QColor &color) const;
    Q_INVOKABLE QColor pressed(const QColor &color) const;

    QColor windowBg() const;
    QColor sidebarBg() const;
    QColor workspaceBg() const;
    QColor surfaceBg() const;
    QColor surfaceAltBg() const;
    QColor surfaceHoverBg() const;
    QColor chromeBg() const;
    QColor overlayBg() const;
    QColor consoleBg() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor textMuted() const;
    QColor textDisabled() const;
    QColor textOnAccent() const;
    QColor textLink() const;
    QColor borderSubtle() const;
    QColor borderStrong() const;
    QColor separator() const;
    QColor accent() const;
    QColor focusRing() const;
    QColor success() const;
    QColor warning() const;
    QColor danger() const;
    QColor info() const;
    QColor neutralOff() const;
    QColor tooltipBg() const;
    QColor tooltipText() const;
    QColor badgeBg() const;
    QColor tabActiveBg() const;
    QColor tabInactiveBg() const;
    QColor selectionBg() const;
    QColor scrollbar() const;
    double radiusCard() const;
    double radiusOverlay() const;
    double radiusControl() const;
    double radiusPill() const;
    double spacingXs() const;
    double spacingS() const;
    double spacingM() const;
    double spacingL() const;
    double spacingXl() const;
    double fontSizeCaption() const;
    double fontSizeSmall() const;
    double fontSizeBody() const;
    double fontSizeSubtitle() const;
    double fontSizeCardTitle() const;
    double fontSizePageTitle() const;
    double cardMinWidth() const;
    double cardHeight() const;
    double durationFast() const;
    double durationNormal() const;
    double sidebarWidth() const;
    double sidebarCollapsedWidth() const;
    double statusBarHeight() const;
    double tabBarHeight() const;
    double toastWidth() const;
    QString family() const;
    QString monoFamily() const;
    QStringList fontFamilies() const;

signals:
    void changed();

private:
    void loadCurrent();

    core::Settings *m_settings;
    ThemeRegistry *m_registry;
    ThemeFile m_current;
};

} // namespace awb::theme

#endif // AWB_THEME_THEME_H
