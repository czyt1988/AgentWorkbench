#ifndef AWB_AGENTS_AGENTMODEL_H
#define AWB_AGENTS_AGENTMODEL_H

#include "agentcatalog/AgentDefinition.h"
#include "agentcatalog/AgentState.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>

namespace awb::agentcatalog {

/// agent 的列表模型：持久化定义 + 按 id 索引的运行期状态。
///
/// role 名与顺序和 0.3.0 逐字节兼容，卡片 QML 因此无需改动。
class AgentModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /// 列表模型的 role。名字与顺序是对 QML 的契约，改动前先确认所有页面。
    enum Roles {
        IdRole = Qt::UserRole + 1,  ///< agent 的 id（agents.json 中的键）
        NameRole,                   ///< 显示名
        CommandRole,                ///< 启动命令
        WebUrlRole,                 ///< Web UI 地址
        ConfigDirRole,              ///< agent 的配置目录
        IconRole,                   ///< 图标 URL
        ColorRole,                  ///< 强调色
        CardColorRole,              ///< 卡片底色
        RunningRole,                ///< 健康检查判定为运行中
        LaunchingRole,              ///< launch() 已发起、尚未确认运行
        InstallCommandRole,         ///< 一次性安装命令
        UpdateCommandRole,          ///< 一次性更新命令
        VersionCommandRole,         ///< 一次性版本查询命令
        SetupCommandRole,           ///< 一次性初始化命令
        InstalledRole,              ///< versionCommand 检测出的已安装
        VersionRole,                ///< 版本号，从 versionCommand 输出解析
        InstallingRole,             ///< 安装进行中
        SetupDoneRole,              ///< setup 已完成
        SetuppingRole,              ///< setup 进行中
        CheckingVersionRole,        ///< 版本查询进行中
        ConsoleOutputRole           ///< install/update/setup 的实时输出
    };
    Q_ENUM(Roles)

    explicit AgentModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 整体替换定义列表；换过后仍存在的 id 保留其运行期状态，
    // 调用方因此不会把运行中的卡片清空
    void setDefinitions(const QList<AgentDefinition> &definitions);
    // 当前的定义列表（AgentHealthMonitor 等只读者用）
    const QList<AgentDefinition> &definitions() const { return m_definitions; }

    // 按 id 原位替换定义并刷新整行；运行期状态按 id 另存，不受影响
    bool replaceDefinition(const AgentDefinition &definition);

    // 在指定行插入定义；row 越界时追加到末尾
    void insertAgent(int row, const AgentDefinition &definition);

    // 删除指定 id 的 agent；找不到返回 false
    bool removeAgentById(const QString &id);

    // id 所在的行号；不存在返回 -1
    Q_INVOKABLE int indexOf(const QString &id) const;
    // 定义与运行期状态合并成单个 map（编辑表单的输入）
    Q_INVOKABLE QVariantMap agent(const QString &id) const;

    // 某 id 的运行期状态；从未记录时返回全默认值
    AgentState state(const QString &id) const { return m_states.value(id); }

public Q_SLOTS:
    // 运行期状态的逐字段写入口：每个槽改 id 状态的一个字段并按对应 role
    // 发 dataChanged；id 未知或值未变时不发信号（与 0.3.0 一致）
    void setRunning(const QString &id, bool running);
    void setLaunching(const QString &id, bool launching);
    void setInstalled(const QString &id, bool installed);
    void setVersion(const QString &id, const QString &version);
    void setInstalling(const QString &id, bool installing);
    void setSetupDone(const QString &id, bool done);
    void setSetupping(const QString &id, bool setupping);
    void setCheckingVersion(const QString &id, bool checking);
    void setConsoleOutput(const QString &id, const QString &text);

private:
    QList<AgentDefinition> m_definitions;  ///< 持久化定义，行序即列表序
    QHash<QString, AgentState> m_states;   ///< id -> 运行期状态，与行序无关
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTMODEL_H
