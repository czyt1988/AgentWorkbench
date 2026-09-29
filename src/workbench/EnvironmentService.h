#ifndef AWB_WORKBENCH_ENVIRONMENTSERVICE_H
#define AWB_WORKBENCH_ENVIRONMENTSERVICE_H

#include <QObject>
#include <QSet>
#include <QString>

namespace awb::core {
class ScriptRunner;
} // namespace awb::core

namespace awb::workbench {

/// 状态栏的 Python / Node.js 检测。
///
/// QML 全局名 `environment`（main.cpp 注册）；探测走 ScriptRunner 的
/// `<程序> --version`，结果经 changed() 一次性广播全部属性。
class EnvironmentService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString pythonVersion READ pythonVersion NOTIFY changed)
    Q_PROPERTY(bool pythonInstalled READ pythonInstalled NOTIFY changed)
    Q_PROPERTY(QString nodeVersion READ nodeVersion NOTIFY changed)
    Q_PROPERTY(bool nodeInstalled READ nodeInstalled NOTIFY changed)
    Q_PROPERTY(bool detecting READ detecting NOTIFY changed)

public:
    // 构造即完成两个运行时的首次探测
    explicit EnvironmentService(QObject *parent = nullptr);

    // Python 的版本串（如 "3.11.4"）；未安装时为空串
    QString pythonVersion() const { return m_pythonVersion; }
    // Python 是否已安装（PATH 上能找到且 --version 可用）
    bool pythonInstalled() const { return m_pythonInstalled; }
    // Node.js 的版本串；未安装时为空串
    QString nodeVersion() const { return m_nodeVersion; }
    // Node.js 是否已安装
    bool nodeInstalled() const { return m_nodeInstalled; }
    // 是否仍有探测在途（两个运行时都出结果后为 false）
    bool detecting() const { return m_detecting; }

    // 重跑两个探测（状态栏的刷新按钮）
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    /**
     * @brief 任一检测结果变化时发射；QML 端全部属性统一绑定它
     */
    void changed();

private:
    // 探测一个运行时：找不到可执行文件时直接置未安装，否则跑 --version
    void detect(const QString &program, const QString &runtimeName);

    core::ScriptRunner *m_runner;   ///< 命令执行器（--version 探测）
    QString m_pythonVersion;        ///< Python 版本串；未安装为空串
    bool m_pythonInstalled = false; ///< Python 是否已安装
    QString m_nodeVersion;          ///< Node.js 版本串；未安装为空串
    bool m_nodeInstalled = false;   ///< Node.js 是否已安装
    /// 在途探测的 key 集合（"environment:Python" / "environment:Node"）。
    ///
    /// 用带 key 的集合而不是计数器：探测在途时再来一次 refresh() 会顶掉
    /// 旧运行（ScriptRunner 的 epoch 丢弃陈旧 finished()），计数器永远
    /// 回不到零、`detecting` 卡在 true；集合是幂等的——顶掉者结束时删的
    /// 是同一个 key。
    QSet<QString> m_inflight;
    bool m_detecting = false; ///< 是否仍有探测在途
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_ENVIRONMENTSERVICE_H
