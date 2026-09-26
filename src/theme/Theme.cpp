#include "theme/Theme.h"

#include "core/Settings.h"
#include "theme/ThemeRegistry.h"

#include <QVariantMap>

namespace awb::theme {

Theme::Theme(core::Settings *settings, ThemeRegistry *registry,
             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_registry(registry)
{
    loadCurrent();

    // External theme changes (settings.json edited elsewhere) re-apply.
    connect(m_settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QLatin1String("appearance.theme"))
                    loadCurrent();
            });
    // Hot reload: a theme file changed on disk (02 §9.1).
    connect(m_registry, &ThemeRegistry::changed, this, &Theme::loadCurrent);
}

void Theme::loadCurrent()
{
    ThemeFile file = m_registry->theme(m_settings->themeId());
    if (!file.isValid()) {
        // Unknown theme id: fall back to the dark default and warn
        // (01-architecture.md §7.2).
        qWarning().noquote() << QStringLiteral(
            "Theme: theme \"%1\" is unknown; falling back to mocha-dark")
            .arg(m_settings->themeId());
        file = m_registry->theme(QStringLiteral("mocha-dark"));
    }
    // Always swap and notify: even for the same id the values on disk may
    // have changed (hot reload, 02 §9.1).
    m_current = file;
    emit changed();
}

QString Theme::variant() const
{
    return m_current.variant;
}

QString Theme::themeId() const
{
    return m_current.id;
}

QStringList Theme::agentPalette() const
{
    return m_current.agentPalette;
}

QVariantList Theme::availableThemes() const
{
    QVariantList result;
    for (const ThemeFile &file : m_registry->themes()) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = file.id;
        entry[QStringLiteral("name")] = file.name;
        entry[QStringLiteral("variant")] = file.variant;
        result.append(entry);
    }
    return result;
}

void Theme::applyTheme(const QString &id)
{
    if (id == m_settings->themeId())
        return;
    // Validate before persisting: saving an unknown id would silently fall
    // back to mocha-dark on every start (01 §7.2 — unknown → fallback+warn
    // is for hand-edited settings, not for the picker).
    if (!m_registry->theme(id).isValid()) {
        qWarning().noquote() << QStringLiteral(
            "Theme: refusing to apply unknown theme \"%1\"").arg(id);
        return;
    }
    m_settings->setThemeId(id);
    m_settings->save();
    // valueChanged fires loadCurrent() for us.
}

QColor Theme::color(const QString &name) const
{
    return m_current.colors.value(name);
}

double Theme::metric(const QString &name) const
{
    return m_current.metrics.value(name, 0.0);
}

QColor Theme::alpha(const QColor &color, qreal a) const
{
    QColor result = color;
    result.setAlphaF(qBound(0.0, a, 1.0));
    return result;
}

QColor Theme::hover(const QColor &color) const
{
    // Dark themes lighten, light themes darken (02 §9.3).
    const qreal step = 0.08;
    QColor result = color;
    const qreal h = result.hueF(), s = result.saturationF();
    qreal l = result.lightnessF();
    l += (variant() == QLatin1String("dark") ? step : -step);
    result.setHslF(h, s, qBound(0.0, l, 1.0));
    return result;
}

QColor Theme::pressed(const QColor &color) const
{
    const qreal step = 0.16;
    QColor result = color;
    const qreal h = result.hueF(), s = result.saturationF();
    qreal l = result.lightnessF();
    l += (variant() == QLatin1String("dark") ? step : -step);
    result.setHslF(h, s, qBound(0.0, l, 1.0));
    return result;
}

QColor Theme::windowBg() const
{
    return m_current.colors.value(QStringLiteral("windowBg"));
}

QColor Theme::sidebarBg() const
{
    return m_current.colors.value(QStringLiteral("sidebarBg"));
}

QColor Theme::workspaceBg() const
{
    return m_current.colors.value(QStringLiteral("workspaceBg"));
}

QColor Theme::surfaceBg() const
{
    return m_current.colors.value(QStringLiteral("surfaceBg"));
}

QColor Theme::surfaceAltBg() const
{
    return m_current.colors.value(QStringLiteral("surfaceAltBg"));
}

QColor Theme::surfaceHoverBg() const
{
    return m_current.colors.value(QStringLiteral("surfaceHoverBg"));
}

QColor Theme::chromeBg() const
{
    return m_current.colors.value(QStringLiteral("chromeBg"));
}

QColor Theme::overlayBg() const
{
    return m_current.colors.value(QStringLiteral("overlayBg"));
}

QColor Theme::consoleBg() const
{
    return m_current.colors.value(QStringLiteral("consoleBg"));
}

QColor Theme::textPrimary() const
{
    return m_current.colors.value(QStringLiteral("textPrimary"));
}

QColor Theme::textSecondary() const
{
    return m_current.colors.value(QStringLiteral("textSecondary"));
}

QColor Theme::textMuted() const
{
    return m_current.colors.value(QStringLiteral("textMuted"));
}

QColor Theme::textDisabled() const
{
    return m_current.colors.value(QStringLiteral("textDisabled"));
}

QColor Theme::textOnAccent() const
{
    return m_current.colors.value(QStringLiteral("textOnAccent"));
}

QColor Theme::textLink() const
{
    return m_current.colors.value(QStringLiteral("textLink"));
}

QColor Theme::borderSubtle() const
{
    return m_current.colors.value(QStringLiteral("borderSubtle"));
}

QColor Theme::borderStrong() const
{
    return m_current.colors.value(QStringLiteral("borderStrong"));
}

QColor Theme::separator() const
{
    return m_current.colors.value(QStringLiteral("separator"));
}

QColor Theme::accent() const
{
    return m_current.colors.value(QStringLiteral("accent"));
}

QColor Theme::focusRing() const
{
    return m_current.colors.value(QStringLiteral("focusRing"));
}

QColor Theme::success() const
{
    return m_current.colors.value(QStringLiteral("success"));
}

QColor Theme::warning() const
{
    return m_current.colors.value(QStringLiteral("warning"));
}

QColor Theme::danger() const
{
    return m_current.colors.value(QStringLiteral("danger"));
}

QColor Theme::info() const
{
    return m_current.colors.value(QStringLiteral("info"));
}

QColor Theme::neutralOff() const
{
    return m_current.colors.value(QStringLiteral("neutralOff"));
}

QColor Theme::tooltipBg() const
{
    return m_current.colors.value(QStringLiteral("tooltipBg"));
}

QColor Theme::tooltipText() const
{
    return m_current.colors.value(QStringLiteral("tooltipText"));
}

QColor Theme::badgeBg() const
{
    return m_current.colors.value(QStringLiteral("badgeBg"));
}

QColor Theme::tabActiveBg() const
{
    return m_current.colors.value(QStringLiteral("tabActiveBg"));
}

QColor Theme::tabInactiveBg() const
{
    return m_current.colors.value(QStringLiteral("tabInactiveBg"));
}

QColor Theme::selectionBg() const
{
    return m_current.colors.value(QStringLiteral("selectionBg"));
}

QColor Theme::scrollbar() const
{
    return m_current.colors.value(QStringLiteral("scrollbar"));
}

double Theme::radiusCard() const
{
    return m_current.metrics.value(QStringLiteral("radiusCard"), 0.0);
}

double Theme::radiusOverlay() const
{
    return m_current.metrics.value(QStringLiteral("radiusOverlay"), 0.0);
}

double Theme::radiusControl() const
{
    return m_current.metrics.value(QStringLiteral("radiusControl"), 0.0);
}

double Theme::radiusPill() const
{
    return m_current.metrics.value(QStringLiteral("radiusPill"), 0.0);
}

double Theme::spacingXs() const
{
    return m_current.metrics.value(QStringLiteral("spacingXs"), 0.0);
}

double Theme::spacingS() const
{
    return m_current.metrics.value(QStringLiteral("spacingS"), 0.0);
}

double Theme::spacingM() const
{
    return m_current.metrics.value(QStringLiteral("spacingM"), 0.0);
}

double Theme::spacingL() const
{
    return m_current.metrics.value(QStringLiteral("spacingL"), 0.0);
}

double Theme::spacingXl() const
{
    return m_current.metrics.value(QStringLiteral("spacingXl"), 0.0);
}

double Theme::fontSizeCaption() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeCaption"), 0.0);
}

double Theme::fontSizeSmall() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeSmall"), 0.0);
}

double Theme::fontSizeBody() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeBody"), 0.0);
}

double Theme::fontSizeSubtitle() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeSubtitle"), 0.0);
}

double Theme::fontSizeCardTitle() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeCardTitle"), 0.0);
}

double Theme::fontSizePageTitle() const
{
    return m_current.metrics.value(QStringLiteral("fontSizePageTitle"), 0.0);
}

double Theme::cardMinWidth() const
{
    return m_current.metrics.value(QStringLiteral("cardMinWidth"), 0.0);
}

double Theme::cardHeight() const
{
    return m_current.metrics.value(QStringLiteral("cardHeight"), 0.0);
}

double Theme::durationFast() const
{
    return m_current.metrics.value(QStringLiteral("durationFast"), 0.0);
}

double Theme::durationNormal() const
{
    return m_current.metrics.value(QStringLiteral("durationNormal"), 0.0);
}

double Theme::sidebarWidth() const
{
    return m_current.metrics.value(QStringLiteral("sidebarWidth"), 0.0);
}

double Theme::sidebarCollapsedWidth() const
{
    return m_current.metrics.value(QStringLiteral("sidebarCollapsedWidth"), 0.0);
}

double Theme::statusBarHeight() const
{
    return m_current.metrics.value(QStringLiteral("statusBarHeight"), 0.0);
}

double Theme::tabBarHeight() const
{
    return m_current.metrics.value(QStringLiteral("tabBarHeight"), 0.0);
}

double Theme::toastWidth() const
{
    return m_current.metrics.value(QStringLiteral("toastWidth"), 0.0);
}

QString Theme::family() const
{
    return m_current.fonts.value(QStringLiteral("family"));
}

QString Theme::monoFamily() const
{
    return m_current.fonts.value(QStringLiteral("monoFamily"));
}

} // namespace awb::theme
