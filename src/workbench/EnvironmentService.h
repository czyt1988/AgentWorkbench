#ifndef AWB_WORKBENCH_ENVIRONMENTSERVICE_H
#define AWB_WORKBENCH_ENVIRONMENTSERVICE_H

#include "workbench/EnvironmentProbe.h"

#include <QObject>
#include <QString>

namespace awb::workbench {

/// 状态栏与设置页的 Python / Node.js 检测。
///
/// QML 全局名 `environment`（main.cpp 注册）。三条设计线：
///  - **探测在后台线程**：worker 是 EnvironmentProbe 的纯函数，经线程池
///    跑，结果由 QFutureWatcher 回到 GUI 线程落地——探测要等子进程，
///    哪怕机器上杀软/DLP 把启动拖到几秒，界面也不掉帧；
///  - **先缓存后校验**：构造时同步恢复上次的结论（首屏立刻有值），
///    start() 再派一轮真探测，**只有结论变了**才重写缓存并发信号；
///  - **失败不冒充「没装」**：探测拿不到结论（超时/起不来）时保留上次
///    的结论，并按 3 s / 15 s / 60 s 自动重试；真卸载了则由那轮探测给出
///    的权威结论（Missing）覆盖。
class EnvironmentService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString pythonStatus READ pythonStatus NOTIFY changed)
    Q_PROPERTY(QString pythonVersion READ pythonVersion NOTIFY changed)
    Q_PROPERTY(QString pythonPath READ pythonPath NOTIFY changed)
    Q_PROPERTY(bool pythonInstalled READ pythonInstalled NOTIFY changed)
    Q_PROPERTY(QString pythonProbeDetail READ pythonProbeDetail NOTIFY changed)
    Q_PROPERTY(QString nodeStatus READ nodeStatus NOTIFY changed)
    Q_PROPERTY(QString nodeVersion READ nodeVersion NOTIFY changed)
    Q_PROPERTY(QString nodePath READ nodePath NOTIFY changed)
    Q_PROPERTY(bool nodeInstalled READ nodeInstalled NOTIFY changed)
    Q_PROPERTY(QString nodeProbeDetail READ nodeProbeDetail NOTIFY changed)
    Q_PROPERTY(bool detecting READ detecting NOTIFY changed)

public:
    // 只恢复缓存，不探测（探测由 start() 派发）
    explicit EnvironmentService(QObject *parent = nullptr);

    // 三个状态取值：探测中或失败（unknown）、权威判定没有（missing）、
    // 可用（found）。unknown 与 missing 必须分开——前者是「不知道」，
    // 界面不能拿红叉冒充后者。
    QString pythonStatus() const { return statusKey(m_python); }
    // Python 的版本串（如 "3.11.4"）；没有结论或未安装时为空串
    QString pythonVersion() const { return m_python.version; }
    // Python 可执行文件的绝对路径；没有结论或未安装时为空串
    QString pythonPath() const { return m_python.path; }
    // Python 是否可用（PATH 上能找到且能报出版本）
    bool pythonInstalled() const { return m_python.installed; }
    // 最近一轮 Python 探测的排查记录（试过哪些候选、失败在哪）：设置页
    // 用它做 tooltip，同时写进日志。诊断串不是面向用户的文案，不走 tr()
    QString pythonProbeDetail() const { return m_pythonDetail; }
    // Node.js 的状态取值；语义同 pythonStatus
    QString nodeStatus() const { return statusKey(m_node); }
    // Node.js 的版本串；没有结论或未安装时为空串
    QString nodeVersion() const { return m_node.version; }
    // Node.js 可执行文件的绝对路径；没有结论或未安装时为空串
    QString nodePath() const { return m_node.path; }
    // Node.js 是否可用
    bool nodeInstalled() const { return m_node.installed; }
    // 最近一轮 Node.js 探测的排查记录
    QString nodeProbeDetail() const { return m_nodeDetail; }
    // 是否有一轮探测在途
    bool detecting() const { return m_detecting; }

    // 派发首次后台探测（main.cpp 装配时调用一次；构造里不做，装配期
    // 起子进程没有必要）
    void start();

    // 立即重测两个运行时（设置页的 Re-detect）；探测在途时忽略
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    /**
     * @brief 任一检测结果变化时发射；QML 端全部属性统一绑定它
     */
    void changed();

private:
    // 派发一轮后台探测（在途时防抖忽略）
    void beginProbe();

    // 把一轮探测结果并入已知状态：变了才写缓存，没结论就安排重试
    void applySnapshot(const EnvironmentSnapshot &snapshot);

    // 探测无结论时的自动重试；重试预算用尽就停手
    void scheduleRetry();

    // 把当前两个结论写进缓存（只在有变化时调用）
    void persistCache();

    // 结论 -> QML 看到的状态字符串（unknown / missing / found）
    static QString statusKey(const RuntimeState &state);

    RuntimeState m_python;      ///< Python 的已知结论（缓存 + 探测合并）
    RuntimeState m_node;        ///< Node.js 的已知结论
    QString m_pythonDetail;     ///< 最近一轮 Python 探测的排查记录
    QString m_nodeDetail;       ///< 最近一轮 Node.js 探测的排查记录
    bool m_detecting = false;   ///< 是否有一轮探测在途
    /// 已消费的自动重试次数。任何一轮拿到权威结论就清零；清不到则在
    /// 预算用尽后停手，等手动 Re-detect 或下次启动。
    int m_retryIndex = 0;
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_ENVIRONMENTSERVICE_H
