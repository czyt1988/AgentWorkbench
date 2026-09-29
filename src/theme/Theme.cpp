#include "theme/Theme.h"

#include "core/Settings.h"
#include "theme/ThemeRegistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QStyleHints>
#include <QVariantMap>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QGuiApplication>
#endif

namespace awb::theme {

// 测试注入的固定深浅变体（空串 = 真实探测）。
QString Theme::s_testVariant;

// 令牌实现总说明：颜色/数值/字体三批 getter 全部是同一件事——从
// m_current 按同名键取值，因此每个实现的注释只写一行 @brief。缺键的
// 缺省行为：颜色为无效 QColor（QHash::value 默认构造）、数值为 0.0
// （value 的显式默认）、字体为空串——第三方主题缺令牌不会崩，只是该
// 令牌退到缺省值。

/**
 * @brief 构造主题单例
 *
 * 构造时立即加载当前主题（Q_PROPERTY 在首帧就要有值），并连三条变化
 * 通路：settings 的 appearance.* 变化（settings.json 在别处被编辑也
 * 到这里）、ThemeRegistry::changed()（主题文件在磁盘上被改动的热重载），
 * 以及系统深浅色翻转（仅 Qt 6.5+ 且跟随模式开启时换主题）。
 *
 * @param settings 应用设置，读 appearance.theme / appearance.followSystem
 *                 / appearance.fontFamily
 * @param registry 主题注册表，提供可用主题与热重载通知
 * @param parent QObject 父项
 */
Theme::Theme(core::Settings *settings, ThemeRegistry *registry,
             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_registry(registry)
{
    loadCurrent();

    // 外部改动（settings.json 在别处被编辑）同样重新应用。
    connect(m_settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QStringLiteral("appearance.theme")
                    || key == QStringLiteral("appearance.followSystem")) {
                    loadCurrent();
                }
                else if (key == QStringLiteral("appearance.fontFamily")) {
                    // 主题文件没变，只需让 family 令牌的绑定刷新。
                    Q_EMIT changed();
                }
            });
    // 热重载：主题文件在磁盘上被改动。
    connect(m_registry, &ThemeRegistry::changed, this, &Theme::loadCurrent);

    // 系统深浅色翻转：跟随模式开启时换到对应变体的基线主题。qobject_cast
    // 兼顾测试环境——tst_theme 的 runner 只有 QCoreApplication，没有
    // QGuiApplication 就不接线（styleHints() 不可用）。
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto *guiApp = qobject_cast<QGuiApplication *>(
            QCoreApplication::instance())) {
        connect(guiApp->styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) {
                    if (m_settings->appearance().followSystem) {
                        loadCurrent();
                    }
                });
    }
#endif
}

/**
 * @brief 重读当前主题并广播 changed()
 *
 * 跟随系统开启时，当前主题由系统深浅色决定：取对应变体的内置基线主题
 * （dark -> mocha-dark、light -> latte-light），appearance.theme 被搁置；
 * 深浅色不可知（Qt 5、无 QGuiApplication、系统回报 Unknown）时回退
 * appearance.theme，行为与关闭跟随时一致。主题 id 未知（settings.json
 * 被手改坏）时回退 mocha-dark 并告警。id 没变也照样换值发信号：磁盘上
 * 的主题文件内容可能已经变了（热重载），按 id 跳过的话热重载就失效了。
 */
void Theme::loadCurrent()
{
    ThemeFile file;
    if (m_settings->appearance().followSystem) {
        file = m_registry->baseline(systemVariant());
    }
    if (!file.isValid()) {
        file = m_registry->theme(m_settings->themeId());
    }
    if (!file.isValid()) {
        // 未知主题 id：回退深色默认并告警。
        qWarning().noquote() << QStringLiteral(
            "Theme: theme \"%1\" is unknown; falling back to mocha-dark")
            .arg(m_settings->themeId());
        file = m_registry->theme(QStringLiteral("mocha-dark"));
    }
    // 无条件换值并通知：同一个 id 的磁盘内容也可能已变（热重载）。
    m_current = file;
    Q_EMIT changed();
}

/**
 * @brief 取当前主题的深浅变体
 */
QString Theme::variant() const
{
    return m_current.variant;
}

/**
 * @brief 取当前主题 id
 */
QString Theme::themeId() const
{
    return m_current.id;
}

/**
 * @brief 取 agent 卡片轮换分配的调色板
 */
QStringList Theme::agentPalette() const
{
    return m_current.agentPalette;
}

/**
 * @brief 取供选择界面用的主题清单
 *
 * 每项是含 id / name / variant / display 四个键的 QVariantMap；顺序
 * 沿用 ThemeRegistry::themes()（内置在前，用户主题按名排序在后）。
 *
 * @return 全部可用主题的条目列表
 */
QVariantList Theme::availableThemes() const
{
    QVariantList result;
    for (const ThemeFile &file : m_registry->themes()) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = file.id;
        entry[QStringLiteral("name")] = file.name;
        entry[QStringLiteral("variant")] = file.variant;
        // 选择框里显示的短标签：用户只需要分辨深浅，主题全名进 tooltip。
        // 未知 variant 回退到主题名，第三方主题不会因此显示成空白。
        if (file.variant == QStringLiteral("dark")) {
            entry[QStringLiteral("display")] = tr("Dark");
        }
        else if (file.variant == QStringLiteral("light")) {
            entry[QStringLiteral("display")] = tr("Light");
        }
        else {
            entry[QStringLiteral("display")] = file.name;
        }
        result.append(entry);
    }
    return result;
}

/**
 * @brief 切换当前主题
 *
 * 写 appearance.theme 并落盘；构造时连好的 valueChanged 会触发
 * loadCurrent() 完成全部令牌重绑。id 与当前相同直接返回（省一次
 * 落盘）；未知 id 拒绝——先落盘再靠回退兜底的话，每次启动都会走
 * 「未知 → 回退 + 告警」路径，那条路径是给手改 settings.json 留的
 * 容错，不该被选择界面触发。
 *
 * @param id 目标主题 id（注册表里必须存在）
 */
void Theme::applyTheme(const QString &id)
{
    if (id == m_settings->themeId()) {
        return;
    }
    // 先验证再落盘：保存未知 id 会让每次启动都静默回退（见上）。
    if (!m_registry->theme(id).isValid()) {
        qWarning().noquote() << QStringLiteral(
            "Theme: refusing to apply unknown theme \"%1\"").arg(id);
        return;
    }
    m_settings->setThemeId(id);
    m_settings->save();
    // valueChanged 会替我们触发 loadCurrent()。
}

/**
 * @brief 按名字取颜色令牌
 *
 * 与逐个 Q_PROPERTY 的 getter 等价，供需要遍历令牌的组件使用。
 *
 * @param name 令牌名（主题 JSON colors 表的键）
 * @return 对应颜色；令牌不存在时为无效 QColor
 */
QColor Theme::color(const QString &name) const
{
    return m_current.colors.value(name);
}

/**
 * @brief 探测系统深浅色变体
 *
 * 测试注入非空时直接返回它；否则经 QStyleHints::colorScheme() 探测
 * （Qt 6.5+，且进程里要有 QGuiApplication——单元测试的 runner 只有
 * QCoreApplication，探测不了）。系统深浅色未知或不可探测时返回空串，
 * 调用方（loadCurrent）据此回退 appearance.theme。
 *
 * @return "dark" / "light"；不可知时空串
 */
QString Theme::systemVariant()
{
    if (!s_testVariant.isEmpty()) {
        return s_testVariant;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // styleHints() 是静态成员，app 只用于确认进程里确有 GUI 应用对象。
    const QGuiApplication *app = qobject_cast<QGuiApplication *>(
        QCoreApplication::instance());
    if (app) {
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        if (scheme == Qt::ColorScheme::Dark) {
            return QStringLiteral("dark");
        }
        if (scheme == Qt::ColorScheme::Light) {
            return QStringLiteral("light");
        }
    }
#endif
    return QString();
}

/**
 * @brief 查询能否跟随系统深浅色
 *
 * @return Qt 6.5+ 为 true（有 QStyleHints::colorScheme）；Qt 5 恒为
 *         false——那条路线没有等价 API，该键被显式降级为忽略
 */
bool Theme::canFollowSystem() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return true;
#else
    return false;
#endif
}

/**
 * @brief 取「跟随系统深浅色」的当前值
 *
 * @return appearance.followSystem
 */
bool Theme::followSystem() const
{
    return m_settings->appearance().followSystem;
}

/**
 * @brief 切换「跟随系统深浅色」
 *
 * 与 setFontFamily() 对称：写设置 + 落盘，changed() 由构造时连好的
 * valueChanged -> loadCurrent() 槽发出。值未变化直接返回。跟随开启后
 * 当前主题换成系统深浅对应的基线主题，关闭则恢复 appearance.theme。
 *
 * @param on true = 跟随系统深浅色
 * @sa setFontFamily
 */
void Theme::setFollowSystem(bool on)
{
    if (on == m_settings->appearance().followSystem) {
        return;
    }
    m_settings->setFollowSystem(on);
    m_settings->save();
}

/**
 * @brief 固定 systemVariant() 的返回值（测试注入）
 *
 * 传空串恢复真实探测。测试进程没有 QGuiApplication，真实探测永远返回
 * 空串，跟随逻辑因此测不到——注入让「深浅色 -> 基线主题」的映射可测。
 *
 * @param variant "dark" / "light"；空串 = 恢复真实探测
 */
void Theme::setSystemVariantForTesting(const QString &variant)
{
    s_testVariant = variant;
}

/**
 * @brief 按名字取数值令牌
 *
 * 与逐个 Q_PROPERTY 的 getter 等价，供需要遍历令牌的组件使用。
 *
 * @param name 令牌名（主题 JSON metrics 表的键）
 * @return 对应数值；令牌不存在时为 0.0
 */
double Theme::metric(const QString &name) const
{
    return m_current.metrics.value(name, 0.0);
}

/**
 * @brief 给颜色叠加透明度
 *
 * @param color 基色（不修改入参）
 * @param a 目标 alpha，0..1，超出范围会被夹紧
 * @return 设好 alpha 的新颜色
 */
QColor Theme::alpha(const QColor &color, qreal a) const
{
    QColor result = color;
    result.setAlphaF(qBound(0.0, a, 1.0));
    return result;
}

/**
 * @brief 取 hover 态的派生色
 *
 * 深色主题提亮、浅色主题压暗（步长 0.08），沿 HSL 的 L 轴移动，保住
 * 色相与饱和度。QML 必须用它而不是 Qt.darker/Qt.lighter——后者的
 * 方向不随变体走。
 *
 * @param color 基色
 * @return 调整明度后的颜色
 */
QColor Theme::hover(const QColor &color) const
{
    // 深色主题提亮，浅色主题压暗。
    const qreal step = 0.08;
    QColor result = color;
    const qreal h = result.hueF(), s = result.saturationF();
    qreal l = result.lightnessF();
    l += (variant() == QStringLiteral("dark") ? step : -step);
    result.setHslF(h, s, qBound(0.0, l, 1.0));
    return result;
}

/**
 * @brief 取按下态的派生色
 *
 * 与 hover() 同一规则，步长加倍（0.16），按压态比 hover 态更醒目。
 *
 * @param color 基色
 * @return 调整明度后的颜色
 * @sa hover
 */
QColor Theme::pressed(const QColor &color) const
{
    const qreal step = 0.16;
    QColor result = color;
    const qreal h = result.hueF(), s = result.saturationF();
    qreal l = result.lightnessF();
    l += (variant() == QStringLiteral("dark") ? step : -step);
    result.setHslF(h, s, qBound(0.0, l, 1.0));
    return result;
}

/**
 * @brief 取窗口背景色
 */
QColor Theme::windowBg() const
{
    return m_current.colors.value(QStringLiteral("windowBg"));
}

/**
 * @brief 取侧栏背景色
 */
QColor Theme::sidebarBg() const
{
    return m_current.colors.value(QStringLiteral("sidebarBg"));
}

/**
 * @brief 取工作区背景色
 */
QColor Theme::workspaceBg() const
{
    return m_current.colors.value(QStringLiteral("workspaceBg"));
}

/**
 * @brief 取表面（卡片等）背景色
 */
QColor Theme::surfaceBg() const
{
    return m_current.colors.value(QStringLiteral("surfaceBg"));
}

/**
 * @brief 取次级表面背景色
 */
QColor Theme::surfaceAltBg() const
{
    return m_current.colors.value(QStringLiteral("surfaceAltBg"));
}

/**
 * @brief 取表面 hover 背景色
 */
QColor Theme::surfaceHoverBg() const
{
    return m_current.colors.value(QStringLiteral("surfaceHoverBg"));
}

/**
 * @brief 取界面 chrome（工具条、状态栏等）背景色
 */
QColor Theme::chromeBg() const
{
    return m_current.colors.value(QStringLiteral("chromeBg"));
}

/**
 * @brief 取遮罩层背景色
 */
QColor Theme::overlayBg() const
{
    return m_current.colors.value(QStringLiteral("overlayBg"));
}

/**
 * @brief 取控制台背景色
 */
QColor Theme::consoleBg() const
{
    return m_current.colors.value(QStringLiteral("consoleBg"));
}

/**
 * @brief 取主要文本色
 */
QColor Theme::textPrimary() const
{
    return m_current.colors.value(QStringLiteral("textPrimary"));
}

/**
 * @brief 取次要文本色
 */
QColor Theme::textSecondary() const
{
    return m_current.colors.value(QStringLiteral("textSecondary"));
}

/**
 * @brief 取弱化文本色
 */
QColor Theme::textMuted() const
{
    return m_current.colors.value(QStringLiteral("textMuted"));
}

/**
 * @brief 取禁用态文本色
 */
QColor Theme::textDisabled() const
{
    return m_current.colors.value(QStringLiteral("textDisabled"));
}

/**
 * @brief 取 accent 之上的文本色
 */
QColor Theme::textOnAccent() const
{
    return m_current.colors.value(QStringLiteral("textOnAccent"));
}

/**
 * @brief 取链接文本色
 */
QColor Theme::textLink() const
{
    return m_current.colors.value(QStringLiteral("textLink"));
}

/**
 * @brief 取弱边框色
 */
QColor Theme::borderSubtle() const
{
    return m_current.colors.value(QStringLiteral("borderSubtle"));
}

/**
 * @brief 取强边框色
 */
QColor Theme::borderStrong() const
{
    return m_current.colors.value(QStringLiteral("borderStrong"));
}

/**
 * @brief 取分隔线颜色
 */
QColor Theme::separator() const
{
    return m_current.colors.value(QStringLiteral("separator"));
}

/**
 * @brief 取强调色
 */
QColor Theme::accent() const
{
    return m_current.colors.value(QStringLiteral("accent"));
}

/**
 * @brief 取焦点环颜色
 */
QColor Theme::focusRing() const
{
    return m_current.colors.value(QStringLiteral("focusRing"));
}

/**
 * @brief 取成功状态色
 */
QColor Theme::success() const
{
    return m_current.colors.value(QStringLiteral("success"));
}

/**
 * @brief 取警告状态色
 */
QColor Theme::warning() const
{
    return m_current.colors.value(QStringLiteral("warning"));
}

/**
 * @brief 取危险/错误状态色
 */
QColor Theme::danger() const
{
    return m_current.colors.value(QStringLiteral("danger"));
}

/**
 * @brief 取信息状态色
 */
QColor Theme::info() const
{
    return m_current.colors.value(QStringLiteral("info"));
}

/**
 * @brief 取中性关闭态色
 */
QColor Theme::neutralOff() const
{
    return m_current.colors.value(QStringLiteral("neutralOff"));
}

/**
 * @brief 取 tooltip 背景色
 */
QColor Theme::tooltipBg() const
{
    return m_current.colors.value(QStringLiteral("tooltipBg"));
}

/**
 * @brief 取 tooltip 文本色
 */
QColor Theme::tooltipText() const
{
    return m_current.colors.value(QStringLiteral("tooltipText"));
}

/**
 * @brief 取徽标背景色
 */
QColor Theme::badgeBg() const
{
    return m_current.colors.value(QStringLiteral("badgeBg"));
}

/**
 * @brief 取激活标签背景色
 */
QColor Theme::tabActiveBg() const
{
    return m_current.colors.value(QStringLiteral("tabActiveBg"));
}

/**
 * @brief 取非激活标签背景色
 */
QColor Theme::tabInactiveBg() const
{
    return m_current.colors.value(QStringLiteral("tabInactiveBg"));
}

/**
 * @brief 取选区背景色
 */
QColor Theme::selectionBg() const
{
    return m_current.colors.value(QStringLiteral("selectionBg"));
}

/**
 * @brief 取选区上的文字色
 *
 * 与 selectionBg 成对保证可读：不设的话 TextEdit 回落到系统 palette 的
 * HighlightedText（Windows 亮色主题下近黑），配深色主题的 selectionBg
 * 几乎不可辨。
 */
QColor Theme::selectionText() const
{
    return m_current.colors.value(QStringLiteral("selectionText"));
}

/**
 * @brief 取滚动条颜色
 */
QColor Theme::scrollbar() const
{
    return m_current.colors.value(QStringLiteral("scrollbar"));
}

/**
 * @brief 取卡片圆角半径
 */
double Theme::radiusCard() const
{
    return m_current.metrics.value(QStringLiteral("radiusCard"), 0.0);
}

/**
 * @brief 取浮层圆角半径
 */
double Theme::radiusOverlay() const
{
    return m_current.metrics.value(QStringLiteral("radiusOverlay"), 0.0);
}

/**
 * @brief 取控件圆角半径
 */
double Theme::radiusControl() const
{
    return m_current.metrics.value(QStringLiteral("radiusControl"), 0.0);
}

/**
 * @brief 取胶囊形圆角半径
 */
double Theme::radiusPill() const
{
    return m_current.metrics.value(QStringLiteral("radiusPill"), 0.0);
}

/**
 * @brief 取超小间距
 */
double Theme::spacingXs() const
{
    return m_current.metrics.value(QStringLiteral("spacingXs"), 0.0);
}

/**
 * @brief 取小间距
 */
double Theme::spacingS() const
{
    return m_current.metrics.value(QStringLiteral("spacingS"), 0.0);
}

/**
 * @brief 取中档间距
 */
double Theme::spacingM() const
{
    return m_current.metrics.value(QStringLiteral("spacingM"), 0.0);
}

/**
 * @brief 取大间距
 */
double Theme::spacingL() const
{
    return m_current.metrics.value(QStringLiteral("spacingL"), 0.0);
}

/**
 * @brief 取超大间距
 */
double Theme::spacingXl() const
{
    return m_current.metrics.value(QStringLiteral("spacingXl"), 0.0);
}

/**
 * @brief 取备注文字字号（字号阶梯的第 4 档，最小）
 */
double Theme::fontSizeCaption() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeCaption"), 0.0);
}

/**
 * @brief 取正文字号（字号阶梯的第 3 档）
 */
double Theme::fontSizeBody() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeBody"), 0.0);
}

/**
 * @brief 取副标题字号（字号阶梯的第 2 档）
 */
double Theme::fontSizeSubtitle() const
{
    return m_current.metrics.value(QStringLiteral("fontSizeSubtitle"), 0.0);
}

/**
 * @brief 取标题字号（字号阶梯的第 1 档，最大）
 */
double Theme::fontSizePageTitle() const
{
    return m_current.metrics.value(QStringLiteral("fontSizePageTitle"), 0.0);
}

/**
 * @brief 取卡片最小宽度
 */
double Theme::cardMinWidth() const
{
    return m_current.metrics.value(QStringLiteral("cardMinWidth"), 0.0);
}

/**
 * @brief 取卡片高度
 */
double Theme::cardHeight() const
{
    return m_current.metrics.value(QStringLiteral("cardHeight"), 0.0);
}

/**
 * @brief 取快档动效时长
 */
double Theme::durationFast() const
{
    return m_current.metrics.value(QStringLiteral("durationFast"), 0.0);
}

/**
 * @brief 取常规动效时长
 */
double Theme::durationNormal() const
{
    return m_current.metrics.value(QStringLiteral("durationNormal"), 0.0);
}

/**
 * @brief 取侧栏展开宽度
 */
double Theme::sidebarWidth() const
{
    return m_current.metrics.value(QStringLiteral("sidebarWidth"), 0.0);
}

/**
 * @brief 取侧栏折叠宽度
 */
double Theme::sidebarCollapsedWidth() const
{
    return m_current.metrics.value(QStringLiteral("sidebarCollapsedWidth"), 0.0);
}

/**
 * @brief 取状态栏高度
 */
double Theme::statusBarHeight() const
{
    return m_current.metrics.value(QStringLiteral("statusBarHeight"), 0.0);
}

/**
 * @brief 取标签栏高度
 */
double Theme::tabBarHeight() const
{
    return m_current.metrics.value(QStringLiteral("tabBarHeight"), 0.0);
}

/**
 * @brief 取 toast 宽度
 */
double Theme::toastWidth() const
{
    return m_current.metrics.value(QStringLiteral("toastWidth"), 0.0);
}

/**
 * @brief 取正文字体族
 *
 * 优先级：用户设置 appearance.fontFamily > 主题 JSON 的 fonts.family；
 * 两者都为空时返回空串，QML 侧跟随系统默认。
 *
 * @return 当前生效的字体族名；无任何来源时为空串
 */
QString Theme::family() const
{
    // 设置覆盖优先：用户在设置页选的字体（appearance.fontFamily）优先于
    // 主题 JSON 的 fonts.family；两者都空 = 跟随系统默认。
    const QString override = m_settings->fontFamily();
    if (!override.isEmpty()) {
        return override;
    }
    return m_current.fonts.value(QStringLiteral("family"));
}

/**
 * @brief 取等宽字体族（主题值，无设置覆盖机制）
 */
QString Theme::monoFamily() const
{
    return m_current.fonts.value(QStringLiteral("monoFamily"));
}

/**
 * @brief 取本机可用字体族清单
 *
 * 进程内不变，因此 Q_PROPERTY 标 CONSTANT（不发 changed）。
 */
QStringList Theme::fontFamilies() const
{
    // Qt 6 起 QFontDatabase 只剩静态接口，Qt 5 是实例接口。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QFontDatabase::families();
#else
    QFontDatabase db;
    return db.families();
#endif
}

/**
 * @brief 切换全局 UI 字体
 *
 * 与 applyTheme() 对称：写设置 + 落盘，changed() 由构造时连好的
 * valueChanged 槽发出，family 令牌随之重绑。值未变化直接返回。
 *
 * @param family 字体族名；空串表示跟随主题（再空则系统默认）
 * @sa applyTheme
 */
void Theme::setFontFamily(const QString &family)
{
    if (family == m_settings->fontFamily()) {
        return;
    }
    m_settings->setFontFamily(family);
    m_settings->save();
}

} // namespace awb::theme
