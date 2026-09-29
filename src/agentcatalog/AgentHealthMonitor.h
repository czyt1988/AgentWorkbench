#ifndef AWB_AGENTS_AGENTHEALTHMONITOR_H
#define AWB_AGENTS_AGENTHEALTHMONITOR_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

namespace awb::core {
class HttpProbe;
} // namespace awb::core

namespace awb::agentcatalog {

class AgentModel;

/// 按固定间隔探测每个带 webUrl 的 agent，把「运行中 / 已停止」的变化广播出去。
///
/// 判定语义沿用 core::HttpProbe：任何 HTTP 响应 = 运行中，连接被拒绝或
/// 超时 = 已停止。只上报状态变化（边沿触发）——日志记录的是 agent 何时
/// 起来、何时挂掉，而不是每一轮探测。不做进程嗅探。
class AgentHealthMonitor : public QObject
{
    Q_OBJECT

public:
    AgentHealthMonitor(AgentModel *model, int intervalMs,
                       QObject *parent = nullptr);

    // 按配置的间隔开始轮询，并立即探测一轮
    void start();

    // 立即再探测一轮（launch/stop 之后卡片要尽快翻转）
    void recheckNow();

Q_SIGNALS:
    /**
     * @brief 某 agent 的运行状态发生翻转时发射
     *
     * 边沿触发：状态不变不发射。BuiltinPages 靠它驱动 WebTabsFacade 的
     * 在线标记，重复发射会把 error 标签无限拉回 loading 重载。
     *
     * @param id agent id
     * @param running true = 探测到 HTTP 响应（运行中）；false = 拒绝/超时
     */
    void runningChanged(const QString &id, bool running);

private:
    // 探测所有 agent 的 webUrl（同 URL 每轮只发一次请求）
    void checkAll();

    AgentModel *m_model;          ///< 被探测的 agent 列表来源
    int m_intervalMs;             ///< 轮询间隔（毫秒）
    core::HttpProbe *m_probe;     ///< 无依赖的 HTTP 探测器
    class QTimer *m_timer;        ///< 轮询定时器
    // 在途探测的 URL 集合。兼任重叠保护：一个 URL 在上一轮应答之前不会
    // 再次探测，慢到的旧应答因此永远盖不过更新的状态；且共享同一 URL 的
    // N 个 agent 每轮只花一次请求，而不是 N 次。
    QSet<QString> m_inflight;
    // id -> 上次上报的状态，让 runningChanged 保持边沿触发：稳定
    // 「运行中」不得每轮重发——否则 BuiltinPages 会把每个 error 标签
    // 拉回 loading 无限重载（token 门禁页面上实际观察到 3 s 一次的
    // 加载-重试死循环）。
    QHash<QString, bool> m_lastRunning;
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTHEALTHMONITOR_H
