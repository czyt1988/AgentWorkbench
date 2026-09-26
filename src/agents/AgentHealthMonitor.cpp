#include "agents/AgentHealthMonitor.h"

#include "agents/AgentModel.h"
#include "core/HttpProbe.h"

#include <QTimer>

namespace awb::agents {

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
                // One probe result applies to every agent pointing at the
                // URL (0.3.0 issued one request per agent; same outcome).
                const QList<AgentDefinition> &definitions =
                    m_model->definitions();
                for (const AgentDefinition &d : definitions) {
                    if (d.webUrl == url)
                        emit runningChanged(d.id, up);
                }
            });
}

void AgentHealthMonitor::start()
{
    checkAll();
    m_timer->start(m_intervalMs);
}

void AgentHealthMonitor::recheckNow()
{
    checkAll();
}

void AgentHealthMonitor::checkAll()
{
    const QList<AgentDefinition> &definitions = m_model->definitions();
    QSet<QString> issued; // one request per URL within this round
    for (const AgentDefinition &d : definitions) {
        if (d.webUrl.isEmpty())
            continue;
        // In-flight from a previous round: skip — its pending answer is the
        // freshest possible for this URL, and re-issuing would let the older
        // reply land last and flip the card wrongly.
        if (m_inflight.contains(d.webUrl) || issued.contains(d.webUrl))
            continue;
        issued.insert(d.webUrl);
        m_inflight.insert(d.webUrl);
        m_probe->probe(d.webUrl);
    }
}

} // namespace awb::agents
