#ifndef AWB_AGENTS_AGENTSCRIPTS_H
#define AWB_AGENTS_AGENTSCRIPTS_H

#include <QHash>
#include <QObject>
#include <QString>

namespace awb::core {
class ScriptRunner;
} // namespace awb::core

namespace awb::agentcatalog {

class AgentModel;
class AgentStateStore;

/// agent 的一次性命令：install / update / version / setup。
///
/// 每条命令都经 core::ScriptRunner 以 "<operation>:<id>" 为 key 运行；
/// 0.3.0 AgentLauncher 里的逐操作规则（日志行、卡片状态、面向用户的
/// 信号）由本类持有。
class AgentScripts : public QObject
{
    Q_OBJECT

public:
    AgentScripts(AgentModel *model, AgentStateStore *stateStore,
                 QObject *parent = nullptr);

    // 每个方法自查前置条件（agent 运行中、命令未配置），行为与 0.3.0
    // 一致，失败经下面的信号上报
    void install(const QString &id);
    void update(const QString &id);
    void runSetup(const QString &id);

    // 跑各 agent 的 versionCommand（先转 spinner，再探测）
    void checkVersions();
    void checkVersion(const QString &id);

Q_SIGNALS:
    /**
     * @brief 安装或更新结束时发射（不论成败）
     *
     * 门面以 0.3.0 的同名信号把它转给 QML。
     *
     * @param id      agent id
     * @param success 命令干净退出为 true
     * @param message 失败原因；成功时为空串
     */
    void installFinished(const QString &id, bool success, const QString &message);

    /**
     * @brief 出现阻碍启动的失败时发射
     *
     * setup 没能启动/运行，或请求安装时 agent 正在运行。
     *
     * @param id      agent id
     * @param message 可展示给用户的原因
     */
    void launchFailed(const QString &id, const QString &message);

    /**
     * @brief 版本探测解析出版本串时发射
     *
     * @param id      agent id
     * @param version 解析出的版本串
     */
    void versionResolved(const QString &id, const QString &version);

    /**
     * @brief 一次性 setup 结束时发射
     *
     * @param id agent id
     * @param ok 成功为 true——setup 完成状态已记入 AgentStateStore，
     *           调用方现在可以启动该 agent
     */
    void setupFinished(const QString &id, bool ok);

private Q_SLOTS:
    // ScriptRunner::finished 的分派：按 key 里的操作更新卡片并发对应信号
    void onScriptFinished(const QString &key, bool ok, int exitCode,
                          const QString &stdOut, const QString &stdErr,
                          const QString &error);
    // ScriptRunner::outputChunk 的分派：把该次运行的输出实时追加到卡片
    void onScriptChunk(const QString &key, const QString &text);

private:
    AgentModel *m_model;            ///< 状态写回的目标模型
    AgentStateStore *m_stateStore;  ///< setup 完成状态的落盘处
    // 每个 scripts 对象独享一个 runner：操作 key 保持模块私有
    core::ScriptRunner *m_runner;

    // 每个脚本运行 key 累计的控制台输出（实时显示用）
    QHash<QString, QString> m_buffers;
    // 每个运行 key 的起始时刻，供 "done, exit=0, 1.2s" 日志行取耗时
    QHash<QString, qint64> m_startMs;
    // setup 的命令文本，留到该次运行结束——失败消息里要引用它
    QHash<QString, QString> m_setupCommands;
    // id -> 版本检查代数，用来让延迟的 spinner 清除失效
    QHash<QString, int> m_versionEpoch;
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTSCRIPTS_H
