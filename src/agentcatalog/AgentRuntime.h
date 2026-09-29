#ifndef AWB_AGENTS_AGENTRUNTIME_H
#define AWB_AGENTS_AGENTRUNTIME_H

#include "agentcatalog/AgentDefinition.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace awb::agentcatalog {

class AgentModel;

/// agent 自身长驻进程的运行器：启动、停止、强制停止与会话级的 PID 记账。
///
/// PID 只存在内存里：健康检查判定为运行中、但并非本启动器会话启动的
/// agent 没有 PID，只能按端口强制停止。
///
/// 带 token 门禁的 harness（dsh）把每进程的鉴权 URL 打到 stdout 而不写
/// token 文件。launch() 把子进程输出重定向到 <logsDir>/output/<id>.log
/// 并轮询该文件，直到出现指向 agent webUrl 同一服务器的 URL
/// （sessionUrlChanged）——Web 表面需要它，裸 webUrl 会被 401 挡回。
class AgentRuntime : public QObject
{
    Q_OBJECT

public:
    explicit AgentRuntime(AgentModel *model, QObject *parent = nullptr);

    // 启动该 agent 的进程（detached，本应用退出后仍存活）；定义配了
    // tokenFile 时把 tokenValue 作为 QWEN_SERVER_TOKEN 交给子进程
    void launch(const AgentDefinition &definition, const QString &tokenValue);

    // 结束本次会话中由此启动的进程树；没有记账的 PID 时返回 false
    // 并发 launchFailed
    bool stop(const QString &id);

    // 杀掉占用该 agent web 端口的进程——对不是本启动器启动的 agent 也
    // 有效（显式用户动作）
    void forceStop(const QString &id);

    // 本次会话是否至少启动过一个 agent
    bool hasLaunchedAgents() const;

    // 结束本次会话启动的全部进程；返回成功杀掉的进程树数量
    int stopAll();

    // 忘掉记账的 PID（agent 进程继续运行——从配置里移除 agent 时用）
    void forget(const QString &id);

    // 监听给定 TCP 端口的进程 PID；forceStop() 背后的端口→PID 解析，
    // 公开出来让映射逻辑可以直接验证（有测试用例）
    static QList<qint64> findPidsForPort(int port);

    // 为该 agent 抓到的会话 URL（无则空串）；进程停止时丢弃——每进程
    // token 已随进程消亡，openWeb 回退到配置的 webUrl
    QString sessionUrl(const QString &id) const;

Q_SIGNALS:
    /**
     * @brief 启动或停止尝试失败时发射
     *
     * 界面据此在对应卡片上原位闪红，并弹出详情说明。
     *
     * @param id 出错的 agent id
     * @param message 可直接展示给用户的失败原因
     */
    void launchFailed(const QString &id, const QString &message);

    /**
     * @brief 请求健康监视器立即复查
     *
     * launch/stop 之后卡片要尽快翻转状态（0.3.0 靠短定时器复查）。
     */
    void recheckRequested();

    /**
     * @brief launch 抓到该 agent 的鉴权会话 URL 时发射
     *
     * dsh 把每进程的带 token URL 打到自己的输出里；每次 launch 至多
     * 发射一次，URL 本身从不进日志。
     *
     * @param id agent id
     * @param url 已合并 token 的会话 URL
     */
    void sessionUrlChanged(const QString &id, const QString &url);

private:
    // 轮询该 id 的输出日志找会话 URL（由 m_sessionUrlTimer 每 500 ms 驱动一次）
    void watchSessionUrl(const QString &id);
    // 丢弃该 id 的会话 URL 与在途监视
    void dropSessionUrl(const QString &id);

    AgentModel *m_model;  ///< 状态写回的目标模型

    // id -> 本启动器最近一次启动的进程 PID（仅在内存、仅当前会话），
    // 启动器重启后即丢失
    QHash<QString, qint64> m_pids;

    // id -> launch 代数，每次成功启动自增；用来让上一次尝试残留的
    // "launching" 安全超时失效
    QHash<QString, int> m_launchEpoch;

    // id -> 本次 launch 从该 agent 输出里抓到的 URL
    QHash<QString, QString> m_sessionUrls;

    // 一个在途的输出监视：要匹配什么、还剩多少个 500 ms 刻度后放弃
    //（有些 agent 从不打印 URL）
    struct SessionWatch {
        QString webUrl;      ///< 要匹配的服务器（配置的 webUrl）
        QString tokenFile;   ///< 抓到 URL 后要合并的 token 文件
        int attemptsLeft;    ///< 剩余轮询次数，耗尽即放弃
    };
    QHash<QString, SessionWatch> m_sessionUrlWatch;  ///< id -> 在途的输出监视

    // 会话 URL 尚在寻找时的输出轮询定时器
    QTimer m_sessionUrlTimer;
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTRUNTIME_H
