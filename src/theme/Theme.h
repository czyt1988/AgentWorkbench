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

/// QML 全局单例 `theme`：把主题的语义令牌以命名属性暴露给 QML。
///
/// 属性名即令牌名，是对页面的契约（页面只写 theme.xxx，不写字面颜色）；
/// 另配动态查表（color/metric）与派生色（alpha/hover/pressed）助手。
/// 所有令牌共用唯一一个 changed() 信号，切换主题时整个界面在运行时
/// 一次性重绑。
class Theme : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString variant READ variant NOTIFY changed)
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    Q_PROPERTY(QVariantList availableThemes READ availableThemes NOTIFY changed)
    Q_PROPERTY(QStringList agentPalette READ agentPalette NOTIFY changed)
    // appearance.followSystem 的当前值：跟随系统深浅色时主题不取
    // appearance.theme，而是取系统深浅对应变体的基线主题
    Q_PROPERTY(bool followSystem READ followSystem NOTIFY changed)
    // 本运行时能否探测系统深浅色（Qt 6.5+ 为真，Qt 5 恒为假）；
    // 设置页据此决定「跟随系统」开关是否可用
    Q_PROPERTY(bool canFollowSystem READ canFollowSystem CONSTANT)

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
    Q_PROPERTY(QColor selectionText READ selectionText NOTIFY changed)
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
    Q_PROPERTY(double fontSizeBody READ fontSizeBody NOTIFY changed)
    Q_PROPERTY(double fontSizeSubtitle READ fontSizeSubtitle NOTIFY changed)
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
    // 构造时加载当前主题，并连好设置变更与主题文件热重载两条通知通路
    Theme(core::Settings *settings, ThemeRegistry *registry,
          QObject *parent = nullptr);

    // 当前主题的深浅变体（"dark" / "light"）
    QString variant() const;
    // 当前主题 id
    QString themeId() const;
    // 供选择界面用的主题清单，每项含 id/name/variant/display
    QVariantList availableThemes() const;
    // agent 卡片按位轮换的调色板（#rrggbb 串）
    QStringList agentPalette() const;

    // 运行时切换主题：写 appearance.theme 并落盘，全部令牌随之重绑
    Q_INVOKABLE void applyTheme(const QString &id);

    // 运行时切换全局字体：写 appearance.fontFamily（空串 = 跟随主题/系统
    // 默认），family 令牌随之重绑
    Q_INVOKABLE void setFontFamily(const QString &family);

    // 运行时切换「跟随系统深浅色」：写 appearance.followSystem 并落盘。
    // 为真时当前主题改由系统深浅色决定（对应变体的内置基线主题），
    // appearance.theme 被搁置；系统翻转深浅色会即时换主题
    Q_INVOKABLE void setFollowSystem(bool on);

    // appearance.followSystem 的当前值
    bool followSystem() const;

    // 本运行时能否探测系统深浅色：Qt 6.5+ 为真，Qt 5 恒为假（该键被
    // 忽略、开关置灰）。CONSTANT——不随会话变化
    bool canFollowSystem() const;

    // 测试注入：固定 systemVariant() 的返回（空串 = 恢复真实探测）。
    // 单元测试的 runner 只有 QCoreApplication，探测不了系统配色
    static void setSystemVariantForTesting(const QString &variant);

    // 按名字取颜色令牌（供遍历令牌的组件用）；未知名字返回无效 QColor
    Q_INVOKABLE QColor color(const QString &name) const;
    // 按名字取数值令牌；未知名字返回 0.0
    Q_INVOKABLE double metric(const QString &name) const;

    // 派生色：alpha 叠加与 hover/press 明暗变化——方向随变体走。QML 必须
    // 用这些而不是 Qt.darker/Qt.lighter（它们不感知变体）
    Q_INVOKABLE QColor alpha(const QColor &color, qreal a) const;
    Q_INVOKABLE QColor hover(const QColor &color) const;
    Q_INVOKABLE QColor pressed(const QColor &color) const;

    // 颜色令牌 getter：从当前主题按同名键取值，缺键返回无效 QColor
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
    QColor selectionText() const;
    QColor scrollbar() const;
    // 数值令牌 getter（圆角/间距/字号/尺寸/时长）：缺键返回 0.0
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
    double fontSizeBody() const;
    double fontSizeSubtitle() const;
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
    // 字体族令牌（空串 = 跟随系统默认）；family 受用户设置覆盖
    QString family() const;
    QString monoFamily() const;
    QStringList fontFamilies() const;

Q_SIGNALS:
    /**
     * @brief 任一令牌的值变化后发射
     *
     * 所有 Q_PROPERTY 共用它做 NOTIFY：主题切换、主题文件热重载、字体
     * 设置变化各走各的入口，最后都发这一个信号，QML 整体重绑。
     */
    void changed();

private:
    // 从注册表重读当前主题并广播 changed()（设置变更与热重载共用）
    void loadCurrent();

    // 探测系统深浅色："dark" / "light"；未知返回空串
    static QString systemVariant();

    core::Settings *m_settings;  ///< 设置：当前主题与字体覆盖的读写
    ThemeRegistry *m_registry;   ///< 主题来源，提供可用主题与热重载通知
    ThemeFile m_current;         ///< 当前生效的主题（全部令牌 getter 的数据源）

    /// 测试注入的 systemVariant 固定值；空串 = 用真实探测
    static QString s_testVariant;
};

} // namespace awb::theme

#endif // AWB_THEME_THEME_H
