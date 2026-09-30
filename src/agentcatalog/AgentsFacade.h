#ifndef AWB_AGENTS_AGENTSFACADE_H
#define AWB_AGENTS_AGENTSFACADE_H

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::theme {
class Theme;
} // namespace awb::theme

namespace awb::agentcatalog {

class AgentHealthMonitor;
class AgentModel;
class AgentRepository;
class AgentRuntime;
class AgentScripts;
class AgentStateStore;

/// agent 功能的 QML 门面。
///
/// 聚合 repository、model、runtime、scripts 与健康监视器，并保留 0.3.0
/// `launcher` 对象的 Q_INVOKABLE 与信号名——既有 QML 只需把前缀改名
/// （`launcher.` -> `agents.`）。
///
/// openWeb 故意不在门面上：打开 Web UI 是跨域的 workbench 意图
/// `workbench.openWeb(id)`。openConfigDir 留下——WorkbenchContext 把它
/// 委托到这里。Python/Node 检测在 EnvironmentService。
class AgentsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    // 本会话的 start() 是否跑（将跑）版本探测：launcher.startupVersionCheck
    // 的会话快照。卡片不再直接消费它——模型 role versionKnown 已经把
    // 「没探测」「探测超时」「探测有结论」表达得更精确；属性保留是门面
    // API 面的兼容（测试也读它）
    Q_PROPERTY(bool versionCheckEnabled READ versionCheckEnabled NOTIFY
                   versionCheckEnabledChanged)
    // launcher.startupVersionCheck 的当前持久化值（设置页开关的回显；
    // 与会话快照刻意分开——开关改的是下一次启动）
    Q_PROPERTY(bool startupVersionCheck READ startupVersionCheck NOTIFY
                   startupVersionCheckChanged)

public:
    // `theme` 提供自动配色的 agentPalette；nullptr 时用内置色板（单元测试）
    AgentsFacade(core::Settings *settings, const QString &dataRoot,
                 theme::Theme *theme = nullptr, QObject *parent = nullptr);

    // 暴露给 QML 的列表模型
    QAbstractItemModel *model() const;
    // 模块内调用方与测试用的带类型访问
    AgentModel *agentModel() const { return m_model; }

    // 启动该 agent；有未完成的一次性 setup 时先跑 setup，成功后自动续上启动
    Q_INVOKABLE void launch(const QString &id);
    // 结束本次会话中由此启动的进程树；没有记账 PID 时返回 false 并发 launchFailed
    Q_INVOKABLE bool stop(const QString &id);
    // 强制停止：杀掉占用该 agent web 端口的进程——对启动器没启动过、
    // 没有 PID 的 agent 也有效
    Q_INVOKABLE void forceStop(const QString &id);
    // 在系统的文件管理器里打开该 agent 的配置目录
    Q_INVOKABLE void openConfigDir(const QString &id);
    // 一次性命令的转发（行为与失败上报见 AgentScripts）
    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void updateTool(const QString &id);
    // 重新探测该 agent 的版本（卡片右键菜单）。不受启动开关限制——手动
    // 触发的检查就是给「超时后停在不知道」的卡片准备的出路
    Q_INVOKABLE void checkVersion(const QString &id);
    // 重置一次性 setup：从 agent_state.json 清掉记录，下次启动前重跑
    Q_INVOKABLE void resetSetup(const QString &id);

    // 本次会话是否至少启动过一个 agent
    Q_INVOKABLE bool hasLaunchedAgents() const;

    // 本会话的版本探测是否开启（launcher.startupVersionCheck 的会话快照）
    bool versionCheckEnabled() const;
    // launcher.startupVersionCheck 的当前持久化值（设置页开关回显）
    bool startupVersionCheck() const;
    // 写 launcher.startupVersionCheck 并落盘（设置页开关）。只影响下一次
    // 启动：本会话的探测已按 start() 时的值决定跑不跑，versionCheckEnabled
    // 不随这个开关变
    Q_INVOKABLE void setStartupVersionCheck(bool on);

    // 结束本次会话启动的全部进程；返回成功杀掉的进程树数量
    Q_INVOKABLE int stopAll();

    // 启动器管理（设置页）。三者都落盘 agents.json，写不进去时返回
    // false（addAgent 另在 id 已存在时返回 false）
    Q_INVOKABLE bool addAgent(const QVariantMap &fields);
    Q_INVOKABLE bool updateAgentFull(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool removeAgent(const QString &id);
    Q_INVOKABLE bool restoreDefaults();

    // id 是否属于随包的 default_agents.json
    Q_INVOKABLE bool isDefaultAgent(const QString &id) const;

    // 从该 agent 的启动输出里抓到的鉴权会话 URL（dsh 等 token 门禁的
    // harness 每进程打印一条）；没抓到时为空串，openWeb 回退到配置的
    // webUrl
    Q_INVOKABLE QString sessionUrl(const QString &id) const;

    // 磁盘上 agents.json 的路径（错误提示里展示用）
    Q_INVOKABLE QString configFilePath() const;

    // 载入状态、套到模型上，然后启动健康轮询、版本检查与运行时检测
    void start();

Q_SIGNALS:
    /**
     * @brief 启动或停止尝试失败时发射
     *
     * 界面据此在对应卡片上原位闪红并弹出详情说明。
     *
     * @param id      出错的 agent id
     * @param message 可展示给用户的失败原因
     */
    void launchFailed(const QString &id, const QString &message);

    /**
     * @brief 安装或更新结束时发射（不论成败）
     *
     * @param id      agent id
     * @param success 命令干净退出为 true
     * @param message 失败原因；成功时为空串
     */
    void installFinished(const QString &id, bool success, const QString &message);

    /**
     * @brief 转发健康检查的状态翻转
     *
     * 状态变化要离开本域，BuiltinPages 才能应用跨域规则（Web 标签的
     * 离线/在线标记）。
     *
     * @param id      agent id
     * @param running true = 健康检查判定运行中
     */
    void runningChanged(const QString &id, bool running);

    /**
     * @brief 某 agent 从配置中删除时发射
     *
     * BuiltinPages 关闭它的标签页。
     *
     * @param id 被删除的 agent id
     */
    void agentRemoved(const QString &id);

    /**
     * @brief launch 抓到该 agent 的鉴权会话 URL 时发射
     *
     * dsh 一类每进程随机 token 的 URL；BuiltinPages 把已打开的标签
     * 重定向到它。
     *
     * @param id  agent id
     * @param url 已合并 token 的会话 URL
     */
    void sessionUrlChanged(const QString &id, const QString &url);

    /**
     * @brief 本会话的版本探测开关快照变化时发射
     *
     * start() 按当时的 launcher.startupVersionCheck 落定快照；卡片据此
     * 决定「未安装」图标是否显示。
     */
    void versionCheckEnabledChanged();

    /**
     * @brief launcher.startupVersionCheck 的持久化值变化时发射
     *
     * settings.json 被外部改写也会到这里（构造时接好的 valueChanged）。
     */
    void startupVersionCheckChanged();

private:
    // 把模型的当前定义写回 agents.json；成败各记一条日志
    bool saveConfig();

    core::Settings *m_settings;     ///< 设置，不持有；健康间隔与版本开关的来源
    AgentRepository *m_repo;        ///< agents.json 的读写与内置同步
    AgentStateStore *m_stateStore;  ///< agent_state.json（setup 完成记录）
    AgentModel *m_model;            ///< 暴露给 QML 的列表模型
    AgentRuntime *m_runtime;        ///< 长驻进程的启动/停止
    AgentScripts *m_scripts;        ///< 一次性命令
    AgentHealthMonitor *m_health;   ///< 健康轮询
    bool m_versionCheckEnabled;     ///< 版本探测开关的会话快照（start() 落定）
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTSFACADE_H
