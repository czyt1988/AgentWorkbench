#include "shell/TrayController.h"

#include "core/Settings.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWindow>

namespace awb::shell {

// 托盘的可见性与隐藏状态都从这里出，QML 只在 onClosing 里问一句
// 「该收进托盘吗」，退出流程始终留在界面侧（见 quitRequested）。
//
// 图标不自己挑资源路径：它复用 QGuiApplication::windowIcon()，也就是
// main.cpp 在装配阶段设好的那份。shell 因此不需要知道应用资源前缀，
// 换图标只改一处。

/**
 * @brief 构造托盘控制器并按当前设置显示图标
 *
 * @param settings 持久化后端（调用方保证非空且存活期覆盖本对象）
 * @param parent   QObject 父项
 */
TrayController::TrayController(core::Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // 开关能在设置页里当场改动，也要响应外部改写 settings.json：两种来路
    // 都表现为一次 valueChanged，这里只管把图标显示出来或收掉。
    connect(m_settings, &core::Settings::valueChanged, this,
            [this](const QString &key) {
                if (key == QStringLiteral("launcher.trayEnabled")) {
                    syncIcon();
                }
            });
    syncIcon();
}

TrayController::~TrayController()
{
    // QMenu 的构造只收 QWidget* 父项，TrayController 不是 widget，菜单得
    // 自己管：在基类析构开始拆连接之前删掉它。
    delete m_menu;
}

/**
 * @brief 设置要收放的主窗口
 *
 * @param window 主窗口；不持有所有权，调用方保证其存活期覆盖本对象。
 *               传空指针等于关掉收放能力（hideToTray 与 canHideToTray
 *               都会因此拒绝工作），而不是崩溃。
 */
void TrayController::setWindow(QWindow *window)
{
    m_window = window;
}

/**
 * @brief 托盘此刻是否可用
 *
 * @return true = 图标已经在系统托盘里；系统没有托盘、launcher.trayEnabled
 *         关掉、或应用没有设置窗口图标时为 false
 */
bool TrayController::available() const
{
    return m_available;
}

/**
 * @brief 主窗口当前是否被收在托盘里
 *
 * @return true = 窗口处于隐藏状态，等待托盘唤回
 */
bool TrayController::hidden() const
{
    return m_hidden;
}

/**
 * @brief 把主窗口收进托盘
 *
 * 托盘不可用或还没设置窗口时直接返回——调用方（QML 的 onClosing）已经把
 * 「不能收就走正常退出」的判断做在前面，这里只做二次防护。
 *
 * @note 窗口隐藏会让 Qt 的「最后一个窗口关闭即退出」判定成立，因此启用
 *       托盘时应用侧必须把 quitOnLastWindowClosed 关掉，退出改由
 *       workbench.quit() 显式触发（见 app/main.cpp）。
 */
void TrayController::hideToTray()
{
    if (!m_available || m_window == nullptr) {
        return;
    }
    m_window->hide();
    applyHidden(true);

    // 第一次收起时留一句气泡：Windows 上托盘图标默认藏在溢出区，
    // 用户很容易以为程序消失了，找不到回来的入口。
    if (!m_noticeShown && m_icon != nullptr && m_icon->supportsMessages()) {
        m_noticeShown = true;
        // Qt 6 删掉了带 QIcon 的 5 参重载，4 参版本在 Qt 5 / 6 都在；托盘
        // 气泡本来就会带上窗口图标，不必显式传。
        m_icon->showMessage(
            QCoreApplication::translate("TrayController", "AgentWorkbench"),
            QCoreApplication::translate(
                "TrayController",
                "AgentWorkbench is still running in the tray. Click the tray "
                "icon to show the window again."),
            QSystemTrayIcon::Information, 5000);
    }
}

/**
 * @brief 从托盘唤回主窗口并抢焦点
 *
 * 窗口被最小化过再 show() 会停在最小化态，所以这条分支先 showNormal()；
 * 最大化状态由 Qt 自己保留，这里不去动它。
 */
void TrayController::showWindow()
{
    if (m_window == nullptr) {
        return;
    }
    if (m_window->visibility() == QWindow::Minimized) {
        m_window->showNormal();
    }
    else {
        m_window->show();
    }
    m_window->raise();
    m_window->requestActivate();
    applyHidden(false);
}

/**
 * @brief 点窗口 × 时是否应该收进托盘
 *
 * @return true = 托盘可用且 launcher.closeToTray 为真；false = 走正常退出
 */
bool TrayController::canHideToTray() const
{
    return m_available && m_window != nullptr
           && m_settings->launcherOptions().closeToTray;
}

/**
 * @brief 按设置与系统能力创建、显示、隐藏托盘图标
 *
 * 图标对象一旦建成就保留（关掉开关只是隐藏），重新打开时菜单与信号连接
 * 都还在。构建失败只有两种情况：系统托盘不存在，或应用没设窗口图标——
 * 后者记一条告警，让「托盘没了」在日志里可查。
 */
void TrayController::syncIcon()
{
    const bool wanted = m_settings->launcherOptions().trayEnabled
                        && QSystemTrayIcon::isSystemTrayAvailable();

    if (wanted && m_icon == nullptr) {
        const QIcon icon = QGuiApplication::windowIcon();
        if (icon.isNull()) {
            qWarning() << "[shell] tray: the application has no window icon, "
                          "tray stays disabled";
        }
        else {
            m_icon = new QSystemTrayIcon(icon, this);
            // 提示文字同时是托盘图标的辅助功能名：Windows 11 默认把新图标收进
            // 「显示隐藏的图标」溢出区，那里只显示名字不显示 tooltip，不设这一
            // 项的话溢出面板里会是一个没有名字的格子，用户无从认出是本应用。
            m_icon->setToolTip(
                QCoreApplication::translate("TrayController", "AgentWorkbench"));

            m_menu = new QMenu;
            m_toggleAction = m_menu->addAction(QString(), this, [this]() {
                if (m_hidden) {
                    showWindow();
                }
                else {
                    hideToTray();
                }
            });
            m_menu->addSeparator();
            m_menu->addAction(
                QCoreApplication::translate("TrayController", "Quit"), this,
                [this]() { Q_EMIT quitRequested(); });
            m_icon->setContextMenu(m_menu);
            refreshToggleText();

            // 单击与双击都当作「切换显示」。Windows 上双击会先发一次
            // Trigger，两次切换在同一方向上是幂等的，观感就是「点一下就
            // 回来」；Context（右键）不用处理，QSystemTrayIcon 自己弹菜单。
            connect(m_icon, &QSystemTrayIcon::activated, this,
                    [this](QSystemTrayIcon::ActivationReason reason) {
                        if (reason != QSystemTrayIcon::Trigger
                            && reason != QSystemTrayIcon::DoubleClick) {
                            return;
                        }
                        if (m_hidden) {
                            showWindow();
                        }
                        else {
                            hideToTray();
                        }
                    });
        }
    }

    const bool nowVisible = (m_icon != nullptr) && wanted;
    if (nowVisible && !m_icon->isVisible()) {
        m_icon->show();
    }
    else if (!nowVisible && m_icon != nullptr && m_icon->isVisible()) {
        m_icon->hide();
        // 图标没了，「收在托盘里」这个状态就不成立了：窗口此时不可见，
        // 但没有任何入口能把它唤回，按显示态处理更安全。
        if (m_hidden) {
            applyHidden(false);
        }
    }

    if (m_available != nowVisible) {
        m_available = nowVisible;
        // 「托盘图标怎么没出现」是本功能最常见的问题，而 Windows 会把新图标
        // 默认收进溢出区，看起来就像功能没生效。三个判据一次性写进日志，
        // 不用重跑就能区分是设置关了、系统托盘不可用，还是图标已经显示、
        // 只是被系统藏进了溢出区。
        qInfo() << "[shell] tray:" << (nowVisible ? "icon shown" : "icon hidden")
                << "| trayEnabled="
                << m_settings->launcherOptions().trayEnabled
                << "| systemTrayAvailable="
                << QSystemTrayIcon::isSystemTrayAvailable()
                << "| iconObjectCreated=" << (m_icon != nullptr);
        Q_EMIT availableChanged();
    }
}

/**
 * @brief 让菜单首项的文字对齐当前的隐藏状态
 */
void TrayController::refreshToggleText()
{
    if (m_toggleAction == nullptr) {
        return;
    }
    m_toggleAction->setText(
        m_hidden ? QCoreApplication::translate("TrayController", "Show Window")
                 : QCoreApplication::translate("TrayController", "Hide Window"));
}

/**
 * @brief 写入隐藏状态并按边沿触发发信号
 *
 * @param hidden 新的隐藏状态；与当前一致时什么都不做（不重复发
 *               hiddenChanged，避免订阅方反复刷新）
 */
void TrayController::applyHidden(bool hidden)
{
    if (m_hidden == hidden) {
        return;
    }
    m_hidden = hidden;
    refreshToggleText();
    Q_EMIT hiddenChanged();
}

} // namespace awb::shell
