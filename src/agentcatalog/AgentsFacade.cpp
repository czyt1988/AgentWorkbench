#include "agentcatalog/AgentsFacade.h"

#include "agentcatalog/AgentHealthMonitor.h"
#include "agentcatalog/AgentModel.h"
#include "agentcatalog/AgentRepository.h"
#include "agentcatalog/AgentRuntime.h"
#include "agentcatalog/AgentScripts.h"
#include "agentcatalog/AgentStateStore.h"
#include "agentcatalog/AgentUrls.h"
#include "core/EnvExpander.h"
#include "core/Settings.h"
#include "theme/Theme.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QDir>
#include <QUrl>

namespace awb::agentcatalog {

namespace {

// "[app] …" 运行日志行，与 0.3.0 逐字节兼容。

/**
 * @brief 拼运行日志的前缀
 *
 * 形如 "[app] start: "。
 *
 * @param tag       分类标签（"cmd" 或 "app"）
 * @param operation 操作名
 * @param id        agent id；空串时省去 id 段
 * @return 可直接拼接消息的前缀
 */
QString logPrefix(const QString &tag, const QString &operation, const QString &id)
{
    return id.isEmpty()
               ? QStringLiteral("[%1] %2: ").arg(tag, operation)
               : QStringLiteral("[%1] %2 \"%3\": ").arg(tag, operation, id);
}

/**
 * @brief 记一条 [app] 级的运行日志
 *
 * @param operation 操作名
 * @param id        agent id；空串表示应用级事件
 * @param message   消息（英文）
 */
void appLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

/**
 * @brief 记一条 [app] 级的告警日志
 *
 * @param operation 操作名
 * @param id        agent id；空串表示应用级事件
 * @param message   消息（英文）
 */
void appLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

/**
 * @brief 从 QML 编辑表单提交的字段 map 构造定义
 *
 * id 由调用方给出（新生成或不可变）；icon 经 resolveIcon 归一，模型
 * 里因此永远是可显示的 URL。
 *
 * @param f  表单字段 map（键与编辑表单一致）
 * @param id 定死的 agent id
 * @return 填好的定义
 */
AgentDefinition definitionFromFields(const QVariantMap &f, const QString &id)
{
    AgentDefinition a;
    a.id = id;
    a.name = f.value(QStringLiteral("name")).toString().trimmed();
    a.command = f.value(QStringLiteral("command")).toString().trimmed();
    a.webUrl = f.value(QStringLiteral("webUrl")).toString().trimmed();
    a.configDir = f.value(QStringLiteral("configDir")).toString().trimmed();
    a.icon = AgentRepository::resolveIcon(
        f.value(QStringLiteral("icon")).toString().trimmed());
    a.color = f.value(QStringLiteral("color")).toString().trimmed();
    a.cardColor = f.value(QStringLiteral("cardColor")).toString().trimmed();
    a.installCommand =
        f.value(QStringLiteral("installCommand")).toString().trimmed();
    a.updateCommand = f.value(QStringLiteral("updateCommand")).toString().trimmed();
    a.versionCommand =
        f.value(QStringLiteral("versionCommand")).toString().trimmed();
    a.setupCommand = f.value(QStringLiteral("setupCommand")).toString().trimmed();
    a.tokenFile = f.value(QStringLiteral("tokenFile")).toString().trimmed();
    return a;
}

} // namespace

/**
 * @brief 构造门面并装配整个 agent 域
 *
 * 组装 repository/stateStore/model/runtime/scripts/health 并接好域内的
 * 信号链：运行时与一次性命令的失败以 0.3.0 的信号名冒泡给 QML；健康
 * 翻转与会话 URL 变化转出本域（BuiltinPages 应用跨域规则）；健康确认
 * 运行后清掉 launching 转圈；setup 成功后续上真正的启动。
 *
 * @param settings 应用设置，取健康检查间隔
 * @param dataRoot 数据根目录
 * @param theme    提供自动配色 agentPalette 的主题；nullptr 用内置色板
 * @param parent   QObject 父项
 */
AgentsFacade::AgentsFacade(core::Settings *settings, const QString &dataRoot,
                           theme::Theme *theme, QObject *parent)
    : QObject(parent)
    , m_repo(new AgentRepository(dataRoot))
    , m_stateStore(new AgentStateStore(dataRoot))
    , m_model(new AgentModel(this))
    , m_runtime(new AgentRuntime(m_model, this))
    , m_scripts(new AgentScripts(m_model, m_stateStore, this))
    , m_health(new AgentHealthMonitor(
          m_model, settings->launcherOptions().healthCheckIntervalMs, this))
{
    // 配置：内置来自随包默认，用户 agent 叠加其上；自动配色取当前主题。
    if (theme) {
        m_repo->setAgentPalette(theme->agentPalette());
    }
    m_repo->load();
    m_model->setDefinitions(m_repo->definitions());

    // 运行期失败以 0.3.0 的信号名冒泡给 QML。
    connect(m_runtime, &AgentRuntime::launchFailed, this, &AgentsFacade::launchFailed);
    connect(m_scripts, &AgentScripts::launchFailed, this, &AgentsFacade::launchFailed);
    connect(m_scripts, &AgentScripts::installFinished, this,
            &AgentsFacade::installFinished);
    connect(m_runtime, &AgentRuntime::recheckRequested, m_health,
            &AgentHealthMonitor::recheckNow);
    // 健康翻转要离开本域，BuiltinPages 才能应用跨域规则（标签的
    // 离线/在线标记）。
    connect(m_health, &AgentHealthMonitor::runningChanged, this,
            &AgentsFacade::runningChanged);
    // token 门禁 agent 打到启动输出里的会话 URL 同样转出本域
    //（BuiltinPages 把已开的标签重定向过去）。
    connect(m_runtime, &AgentRuntime::sessionUrlChanged, this,
            &AgentsFacade::sessionUrlChanged);

    // 健康翻转：服务起来后清 launching 转圈；日志只记真实的翻转；
    // 模型同步更新。
    connect(m_health, &AgentHealthMonitor::runningChanged, this,
            [this](const QString &id, bool up) {
                if (up) {
                    m_model->setLaunching(id, false);
                }
                const int row = m_model->indexOf(id);
                const bool wasRunning =
                    row >= 0 && m_model->state(id).running;
                if (up != wasRunning) {
                    appLog(QStringLiteral("state"), id,
                           up ? QStringLiteral("stopped → running")
                              : QStringLiteral("running → stopped"));
                }
                m_model->setRunning(id, up);
            });

    // 一次性 setup 成功后接着执行真正的启动。
    connect(m_scripts, &AgentScripts::setupFinished, this,
            [this](const QString &id, bool ok) {
                if (ok) {
                    launch(id);
                }
            });

}

/**
 * @brief 取暴露给 QML 的列表模型
 *
 * @return agent 列表模型（AgentModel，以 QAbstractItemModel 暴露）
 */
QAbstractItemModel *AgentsFacade::model() const
{
    return m_model;
}

/**
 * @brief 启动门面：应用状态并开跑各后台探测
 *
 * 把 agent_state.json 里的 setup 状态套到卡片上；给配了 versionCommand
 * 的 agent 预先标上 "checking"（首帧就有 spinner，版本进程先于首次渲染
 * 结束也不闪空）；最后启动健康轮询与版本检查。
 */
void AgentsFacade::start()
{
    appLog(QStringLiteral("start"), QString(),
           QStringLiteral("%1 launcher(s) configured, config: %2")
               .arg(m_model->definitions().size())
               .arg(configFilePath()));

    // 把持久化的 setup 状态套到卡片上。
    m_stateStore->load();
    for (const AgentDefinition &d : m_model->definitions()) {
        m_model->setSetupDone(d.id, m_stateStore->isSetupDone(d.id));
    }

    // 任何 QML 绘制之前，先把配了 versionCommand 的 agent 全标成
    // "checking"：spinner 从第一帧就可见，即便版本进程在首次渲染前
    // 就已经结束。
    for (const AgentDefinition &d : m_model->definitions()) {
        if (!d.versionCommand.isEmpty()) {
            m_model->setCheckingVersion(d.id, true);
        }
    }

    m_health->start();
    m_scripts->checkVersions();
}

// --- Launch / stop ------------------------------------------------------------

/**
 * @brief 启动该 agent
 *
 * 配了一次性 setup 且尚未跑过时先跑 setup——setupFinished(ok) 会回调
 * 到这里续上真正的启动。tokenFile 的 token 在此刻读出交给运行器。
 *
 * @param id agent id；不在模型中时静默返回
 */
void AgentsFacade::launch(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    const AgentDefinition def = m_model->definitions().at(row);

    // 有尚未跑过的一次性 setup 命令时先跑它；setupFinished(ok) 回调
    // 到 launch()。
    if (!def.setupCommand.isEmpty() && !m_stateStore->isSetupDone(id)) {
        m_scripts->runSetup(id);
        return;
    }

    m_runtime->launch(def, AgentUrls::tokenValue(def.tokenFile));
}

/**
 * @brief 结束本次会话中由此启动的进程树（转发 AgentRuntime::stop）
 *
 * @param id agent id
 * @return 有记账 PID 并成功发起 kill 返回 true；否则 false（原因经
 *         launchFailed 上报）
 */
bool AgentsFacade::stop(const QString &id)
{
    return m_runtime->stop(id);
}

/**
 * @brief 按端口强制结束该 agent 的进程（转发 AgentRuntime::forceStop）
 *
 * @param id agent id
 */
void AgentsFacade::forceStop(const QString &id)
{
    m_runtime->forceStop(id);
}

// openWeb 有意缺席：打开 Web UI 是跨域的 workbench 意图
// `workbench.openWeb(id)`，绝不走门面。

/**
 * @brief 在系统文件管理器里打开该 agent 的配置目录
 *
 * configDir 经 EnvExpander 展开（支持 %VAR% 与 ~）；未配置时记告警
 * 返回，打开成败也各记一条。
 *
 * @param id agent id；不在模型中时静默返回
 */
void AgentsFacade::openConfigDir(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return;
    }
    QString dir = core::EnvExpander::expand(
        m_model->definitions().at(row).configDir);
    if (dir.isEmpty()) {
        appLogError(QStringLiteral("openConfigDir"), id,
                    QStringLiteral("no config directory configured"));
        return;
    }
    dir = QDir::fromNativeSeparators(dir);
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        appLog(QStringLiteral("openConfigDir"), id,
               QStringLiteral("opened %1").arg(dir));
    } else {
        appLogError(QStringLiteral("openConfigDir"), id,
                    QStringLiteral("failed to open %1").arg(dir));
    }
}

/**
 * @brief 判断本次会话是否启动过 agent（转发 AgentRuntime）
 *
 * @return 至少记着一个 PID 时返回 true
 */
bool AgentsFacade::hasLaunchedAgents() const
{
    return m_runtime->hasLaunchedAgents();
}

/**
 * @brief 结束本次会话启动的全部进程（转发 AgentRuntime::stopAll）
 *
 * @return 成功杀掉的进程树数量
 */
int AgentsFacade::stopAll()
{
    return m_runtime->stopAll();
}

// --- 一次性命令转发 -----------------------------------------------------------

/**
 * @brief 运行该 agent 的安装命令（转发 AgentScripts::install）
 *
 * @param id agent id
 */
void AgentsFacade::install(const QString &id)
{
    m_scripts->install(id);
}

/**
 * @brief 运行该 agent 的更新命令（转发 AgentScripts::update）
 *
 * @param id agent id
 */
void AgentsFacade::updateTool(const QString &id)
{
    m_scripts->update(id);
}

/**
 * @brief 重置该 agent 的一次性 setup
 *
 * 卡片翻回未初始化、agent_state.json 里的记录清掉：下次启动前 setup
 * 会重跑。状态文件不存在时静默返回（与 0.3.0 一致——没记录过就没
 * 可清的）。
 *
 * @param id agent id
 */
void AgentsFacade::resetSetup(const QString &id)
{
    m_model->setSetupDone(id, false);

    // 从 agent_state.json 移除；文件不存在 = 没有可清的（静默，
    // 与 0.3.0 一致）。
    if (!QFile::exists(m_stateStore->stateFilePath())) {
        return;
    }

    if (m_stateStore->reset(id)) {
        appLog(QStringLiteral("setup"), id,
               QStringLiteral("re-initialized, the setup command runs again "
                              "before the next start"));
    } else {
        appLogError(QStringLiteral("setup"), id,
                    QStringLiteral("cannot write %1 while clearing the setup "
                                   "state")
                        .arg(m_stateStore->stateFilePath()));
    }
}

// --- CRUD（设置页） -------------------------------------------------------------

/**
 * @brief 新增一个 agent
 *
 * id 由表单给出，为空时从显示名生成 slug 并去重（-2、-3 … 后缀）；
 * 已存在的 id 直接拒绝。color 为空时当场从当前主题的色板取一个——
 * 不等下次 load()，卡片不会先渲染坏一拍。落盘失败则回滚插入，模型
 * 与磁盘保持一致。
 *
 * @param fields 编辑表单的字段 map
 * @return 新增并落盘成功返回 true；id 重复或写不进 agents.json 返回 false
 */
bool AgentsFacade::addAgent(const QVariantMap &fields)
{
    QString id = fields.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        // 从显示名生成，用 -2、-3 … 后缀去重。
        const QString base = AgentRepository::slugFromName(
            fields.value(QStringLiteral("name")).toString());
        id = base;
        int n = 2;
        while (m_model->indexOf(id) >= 0) {
            id = base + QLatin1Char('-') + QString::number(n++);
        }
    } else if (m_model->indexOf(id) >= 0) {
        return false; // id 重复（表单已防；这里是防御）
    }

    AgentDefinition a = definitionFromFields(fields, id);
    // color 为空会让卡片坏掉一拍直到下次重启（load() 才补色板）——
    // 当场取一个，而且取当前主题的色板而非静态 Mocha 回退。
    if (a.color.isEmpty()) {
        a.color = m_repo->paletteColorFor(m_model->definitions().size());
    }

    m_model->insertAgent(m_model->definitions().size(), a);
    if (saveConfig()) {
        return true;
    }
    // 落盘失败：撤销插入，模型与磁盘保持一致（saveConfig() 已覆写的
    // 仓库副本也重新同步）。
    m_model->removeAgentById(id);
    m_repo->setDefinitions(m_model->definitions());
    return false;
}

/**
 * @brief 整体更新该 agent 的定义
 *
 * id 不可变（字段里的 id 被忽略）；color 为空时按行位取当前主题的
 * 色板色。落盘失败则把旧定义放回去，表单丢弃的编辑不会半留驻内存。
 * 运行期状态按 id 另存，换定义不受影响。
 *
 * @param id     要更新的 agent id
 * @param fields 编辑表单的字段 map
 * @return 更新并落盘成功返回 true；id 不在模型中或写不进返回 false
 */
bool AgentsFacade::updateAgentFull(const QString &id, const QVariantMap &fields)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return false;
    }

    AgentDefinition a = definitionFromFields(fields, id); // id 不可变
    if (a.color.isEmpty()) {
        a.color = m_repo->paletteColorFor(row);
    }

    const AgentDefinition previous = m_model->definitions().at(row);
    // 运行期状态按 id 另存，换定义不受影响。
    m_model->replaceDefinition(a);
    if (saveConfig()) {
        return true;
    }
    // 落盘失败：放回旧定义，表单被丢弃的编辑不会半留驻内存。
    m_model->replaceDefinition(previous);
    m_repo->setDefinitions(m_model->definitions());
    return false;
}

/**
 * @brief 从配置中删除该 agent
 *
 * 被删的内置 agent 记进 removed 列表——内置每次启动都按随包默认重新
 * 生成，只有记住 id 才能保持这一个处于删除状态。落盘失败则整体回滚
 *（模型、removed 记录）。进程本身有意继续运行（界面上有说明），只
 * 忘掉 PID 记账。
 *
 * @param id agent id
 * @return 删除并落盘成功返回 true；id 不在模型中或写不进返回 false
 */
bool AgentsFacade::removeAgent(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0) {
        return false;
    }
    const AgentDefinition previous = m_model->definitions().at(row);
    if (!m_model->removeAgentById(id)) {
        return false;
    }

    // 记下被删的内置 agent：内置每次启动都按随包默认重新套用，必须
    // 在这里记住 id 才能让这一个保持删除。
    const bool wasDefault = m_repo->isDefaultAgent(id);
    const QStringList removedBefore = m_repo->removedIds();
    if (wasDefault && !removedBefore.contains(id)) {
        m_repo->setRemovedIds(removedBefore + QStringList{id});
    }

    if (!saveConfig()) {
        // 落盘失败：放回定义与 removed 记录——磁盘上还有它，模型里
        // 也得有。
        m_model->insertAgent(row, previous);
        if (wasDefault) {
            m_repo->setRemovedIds(removedBefore);
        }
        m_repo->setDefinitions(m_model->definitions());
        return false;
    }

    // 进程本身有意继续运行（界面上有说明）。
    m_runtime->forget(id);
    Q_EMIT agentRemoved(id);
    return true;
}

/**
 * @brief 恢复默认 agent 列表
 *
 * 忘掉删除记录，把随包内置列表叠回当前定义上——用户自建 agent 保留。
 * 模型整体替换时按 id 保留运行期状态，运行中的卡片继续运行。
 *
 * @return 落盘并更新模型成功返回 true；写不进 agents.json 返回 false
 */
bool AgentsFacade::restoreDefaults()
{
    // 忘掉删除、重新套用随包内置列表；用户自建的 agent 保留。模型
    // 换血按 id 保留运行期状态，运行中的卡片不受影响。
    if (!m_repo->restoreDefaults(m_model->definitions())) {
        appLogError(QStringLiteral("config"), QString(),
                    QStringLiteral("failed to write %1").arg(configFilePath()));
        return false;
    }
    m_model->setDefinitions(m_repo->definitions());
    appLog(QStringLiteral("config"), QString(),
           QStringLiteral("saved %1").arg(configFilePath()));
    return true;
}

/**
 * @brief 判断 id 是否为随包默认 agent（转发 AgentRepository）
 *
 * @param id agent id
 * @return 属于 default_agents.json 时返回 true
 */
bool AgentsFacade::isDefaultAgent(const QString &id) const
{
    return m_repo->isDefaultAgent(id);
}

/**
 * @brief 取该 agent 抓到的会话 URL（转发 AgentRuntime）
 *
 * @param id agent id
 * @return 已合并 token 的会话 URL；本次会话没抓到返回空串
 */
QString AgentsFacade::sessionUrl(const QString &id) const
{
    return m_runtime->sessionUrl(id);
}

/**
 * @brief 取磁盘上 agents.json 的路径（转发 AgentRepository）
 *
 * @return <dataRoot>/agents.json
 */
QString AgentsFacade::configFilePath() const
{
    return m_repo->configFilePath();
}

/**
 * @brief 把模型的当前定义写回 agents.json
 *
 * 模型内容先同步进仓库再落盘；成败各记一条 [app] 日志。
 *
 * @return 落盘成功返回 true
 */
bool AgentsFacade::saveConfig()
{
    m_repo->setDefinitions(m_model->definitions());
    const bool ok = m_repo->save();
    if (ok) {
        appLog(QStringLiteral("config"), QString(),
               QStringLiteral("saved %1").arg(configFilePath()));
    } else {
        appLogError(QStringLiteral("config"), QString(),
                    QStringLiteral("failed to write %1").arg(configFilePath()));
    }
    return ok;
}

} // namespace awb::agentcatalog
