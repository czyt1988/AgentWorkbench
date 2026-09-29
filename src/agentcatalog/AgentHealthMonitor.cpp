#include "agentcatalog/AgentHealthMonitor.h"

#include "agentcatalog/AgentModel.h"
#include "core/HttpProbe.h"

#include <QTimer>

namespace awb::agentcatalog {

/**
 * @brief 构造健康监视器
 *
 * 构造后不开始探测，等 start() 按间隔驱动。探测结果按 URL 归并：同一条
 * URL 的应答一次性套用到所有指向它的 agent，并对每个 agent 做边沿触发
 * 的去重后才发射 runningChanged。
 *
 * @param model     agent 列表来源，探测时实时读它的 definitions()
 * @param intervalMs 轮询间隔（毫秒）
 * @param parent    QObject 父项
 */
AgentHealthMonitor::AgentHealthMonitor(AgentModel *model, int intervalMs,
                                       QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_intervalMs(intervalMs)
    , m_probe(new core::HttpProbe(this))
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &AgentHealthMonitor::checkAll);
    connect(m_probe, &core::HttpProbe::finished, this,
            [this](const QString &url, bool up) {
                m_inflight.remove(url);
                // 一条探测结果套用到所有指向该 URL 的 agent（0.3.0 是每个
                // agent 发一次请求，结果相同）。每个 agent 各自边沿触发：
                // id 的首次判定与每次真实翻转才发射，稳定状态保持静默。
                const QList<AgentDefinition> &definitions =
                    m_model->definitions();
                for (const AgentDefinition &d : definitions) {
                    if (d.webUrl != url) {
                        continue;
                    }
                    const bool was = m_lastRunning.value(d.id, !up);
                    m_lastRunning.insert(d.id, up);
                    if (up != was) {
                        Q_EMIT runningChanged(d.id, up);
                    }
                }
            });
}

/**
 * @brief 开始按间隔轮询
 *
 * 先立即探测一轮（启动时尽快有状态），再启动定时器。
 */
void AgentHealthMonitor::start()
{
    checkAll();
    m_timer->start(m_intervalMs);
}

/**
 * @brief 立即探测一轮
 *
 * 不动定时器；launch/stop 之后调它让卡片尽快翻转。
 */
void AgentHealthMonitor::recheckNow()
{
    checkAll();
}

/**
 * @brief 探测所有 agent 的 webUrl
 *
 * 同一条 URL 在一轮内只发一次请求；上一轮仍在途的 URL 跳过——它的在途
 * 应答就是该 URL 当前最新的答案，重发反而会让较旧的应答后到、把卡片翻错。
 * 空 webUrl 的 agent 不探测。
 */
void AgentHealthMonitor::checkAll()
{
    const QList<AgentDefinition> &definitions = m_model->definitions();
    QSet<QString> issued; // 本轮内每条 URL 只发一次
    for (const AgentDefinition &d : definitions) {
        if (d.webUrl.isEmpty()) {
            continue;
        }
        if (m_inflight.contains(d.webUrl) || issued.contains(d.webUrl)) {
            continue;
        }
        issued.insert(d.webUrl);
        m_inflight.insert(d.webUrl);
        m_probe->probe(d.webUrl);
    }
}

} // namespace awb::agentcatalog
