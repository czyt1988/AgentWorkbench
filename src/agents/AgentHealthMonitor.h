#ifndef AWB_AGENTS_AGENTHEALTHMONITOR_H
#define AWB_AGENTS_AGENTHEALTHMONITOR_H

#include <QObject>
#include <QString>

namespace awb::core {
class HttpProbe;
} // namespace awb::core

namespace awb::agents {

class AgentModel;

// HTTP health polling for every agent with a webUrl (01-architecture.md
// §4.3). Semantics stay with core::HttpProbe: any HTTP response = running,
// refused/timeout = stopped. Only transitions are reported — the log tells
// the story of when an agent came up or went down, not every poll.
class AgentHealthMonitor : public QObject
{
    Q_OBJECT

public:
    AgentHealthMonitor(AgentModel *model, int intervalMs,
                       QObject *parent = nullptr);

    // Start polling on the configured interval and probe immediately.
    void start();

    // Probe right now (after a launch/stop the cards should flip fast).
    void recheckNow();

signals:
    void runningChanged(const QString &id, bool running);

private:
    void checkAll();

    AgentModel *m_model;
    int m_intervalMs;
    core::HttpProbe *m_probe;
    class QTimer *m_timer;
};

} // namespace awb::agents

#endif // AWB_AGENTS_AGENTHEALTHMONITOR_H
