#ifndef AWB_WEB_WEBTAB_H
#define AWB_WEB_WEBTAB_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

namespace awb::web {

/// 一个打开的 Web 标签：纯状态对象（属性 + 状态机），不含视图。
///
/// 全部属性带 NOTIFY，标签栏与表面在状态机推进时自动重绑。状态机取值：
/// loading（应加载，表面进入该态时触发 reload）→ ready（加载成功或被
/// 主动停止）；offline（健康检查判定 agent 下线）、crashed（渲染进程
/// 崩溃）、error（加载失败）是异常态；released 是 LRU 内存策略的释放态
/// （视图已销毁、标签保留，等用户重新打开）。写入端只归 WebTabsFacade，
/// 表面经它回报；状态迁移规则见 WebTabsFacade.cpp。
class WebTab : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString agentId READ agentId CONSTANT)
    Q_PROPERTY(QUrl url READ url NOTIFY urlChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString iconSource READ iconSource NOTIFY iconSourceChanged)
    Q_PROPERTY(QString color READ color NOTIFY colorChanged)
    Q_PROPERTY(QString surfaceKind READ surfaceKind NOTIFY surfaceKindChanged)
    // 状态机当前值：loading | ready | offline | crashed | error | released
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY loadProgressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)

public:
    WebTab(const QString &id, const QString &agentId, const QUrl &url,
           const QString &title, const QString &iconSource,
           const QString &color, const QString &surfaceKind,
           QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString agentId() const { return m_agentId; }
    QUrl url() const { return m_url; }
    QString title() const { return m_title; }
    QString iconSource() const { return m_iconSource; }
    QString color() const { return m_color; }
    QString surfaceKind() const { return m_surfaceKind; }
    QString state() const { return m_state; }
    int loadProgress() const { return m_loadProgress; }
    QString lastError() const { return m_lastError; }
    double zoom() const { return m_zoom; }

    // 写入端——只由 WebTabsFacade 调用（表面把进度/标题等经它回报）；
    // 值未变化时都不发信号（边沿触发）
    void setUrl(const QUrl &url);
    void setTitle(const QString &title);
    void setState(const QString &state);
    void setLoadProgress(int progress);
    void setLastError(const QString &error);
    void setZoom(double zoom);

    // 最近一次激活的墙钟毫秒，LRU 释放策略比较新旧用；touch() 刷新它
    qint64 lastUsedMs() const { return m_lastUsedMs; }
    void touch() { m_lastUsedMs = QDateTime::currentMSecsSinceEpoch(); }

Q_SIGNALS:
    /**
     * @brief url 变化时发射，表面的 url 绑定据此重新导航
     */
    void urlChanged();

    /**
     * @brief 标题变化时发射（页面回报标题后经 setTabTitle 写入）
     */
    void titleChanged();

    /**
     * @brief iconSource 变化时发射（值来自 agent 定义，创建后不变）
     */
    void iconSourceChanged();

    /**
     * @brief color 变化时发射（值来自 agent 定义，创建后不变）
     */
    void colorChanged();

    /**
     * @brief surfaceKind 变化时发射（createTab 恒传 "embedded"）
     */
    void surfaceKindChanged();

    /**
     * @brief 状态机推进时发射
     *
     * 表面监听它：进入 loading 触发 reload——url 绑定只在 URL 变化时
     * 生效，不改 URL 的重载（reloadTab、markOnlineForAgent）全靠这条
     * 路径；released 令视图隐藏。
     */
    void stateChanged();

    /**
     * @brief 加载进度变化时发射（0..100）
     */
    void loadProgressChanged();

    /**
     * @brief 最近一次错误说明变化时发射（crashed / error 态下展示）
     */
    void lastErrorChanged();

    /**
     * @brief 缩放系数变化时发射
     */
    void zoomChanged();

private:
    QString m_id;       ///< 标签 id，形如 "tab-<n>"，模型与 QML 侧的稳定键
    QString m_agentId;  ///< 所属 agent 的 id
    QUrl m_url;         ///< 当前 URL（可含 token 片段，展示时必须脱敏）
    QString m_title;    ///< 标签显示标题
    QString m_iconSource;  ///< 标签图标（agent 定义）
    QString m_color;    ///< agent 颜色（标签按钮底色）
    QString m_surfaceKind;  ///< 呈现方式 kind（external / embedded）
    QString m_state = QStringLiteral("loading");  ///< 状态机当前值，见类说明
    int m_loadProgress = 0;  ///< 加载进度 0..100
    QString m_lastError;     ///< crashed / error 态的展示文案，无错误为空
    double m_zoom = 1.0;     ///< 缩放系数，setZoom 夹到 [0.5, 2.0]
    qint64 m_lastUsedMs = 0; ///< 最近一次激活的墙钟毫秒（touch() 刷新）
};

} // namespace awb::web

#endif // AWB_WEB_WEBTAB_H
