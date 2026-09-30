#ifndef AWB_WORKBENCH_ENVIRONMENTPROBE_H
#define AWB_WORKBENCH_ENVIRONMENTPROBE_H

#include <QString>

namespace awb::workbench {

/// 一个运行时的已知结论（从缓存读出的与探测得到的形状相同）。
struct RuntimeState
{
    bool known = false;     ///< 是否有权威结论；false 时下面三个字段都无意义
    bool installed = false; ///< known 且 PATH 上有可用的该运行时
    QString version;        ///< 版本串（如 "3.11.4"、"22.20.0"）；否则为空
    QString path;           ///< 可执行文件路径；否则为空

    // 结论相等：服务靠它判断本轮是否有更新——没更新就不重写缓存、
    // 界面也不重排。
    bool operator==(const RuntimeState &other) const;
    bool operator!=(const RuntimeState &other) const;
};

/// 一次运行时探测的结论。
struct RuntimeProbe
{
    /// 探测结论的三种可能。
    enum class Status
    {
        Unknown, ///< 没拿到结论（超时/起不来/预算用尽）——不代表没安装
        Missing, ///< 权威判定：PATH 上没有可用的该运行时
        Found    ///< 拿到了版本
    };

    Status status = Status::Unknown; ///< 本轮结论
    QString version;                 ///< Found 时的版本串
    QString path;                    ///< Found 时的可执行文件路径
    /// 本轮试过什么、失败在哪，只用于日志与界面 tooltip；不含用户数据
    QString detail;
};

/// 一轮探测的结果（两个运行时各一份）。
struct EnvironmentSnapshot
{
    RuntimeProbe python; ///< Python 的结论
    RuntimeProbe node;   ///< Node.js 的结论
};

/// 运行时探测的纯计算 worker。
///
/// 不持状态、不发信号、不碰 QObject：输入只有 PATH，输出是值类型快照，
/// 因此整个 run() 可以丢进线程池（EnvironmentService 负责派发与落地）。
/// 探测直接用解析出来的可执行文件、不经 cmd.exe——只有 npm 风格的
/// .cmd/.bat 垫片例外（CreateProcess 起不了批处理，得包一层 cmd /c，
/// 与 AgentRuntime 的启动路径同一条规则）。
class EnvironmentProbe
{
public:
    // 同步探测两个运行时。线程安全：只读 PATH，只写局部状态与返回值。
    static EnvironmentSnapshot run();

    // 把一次探测结论并入已知状态。
    //
    // Unknown 原样保留 current：探测失败（超时、起不来）与「已经卸载」
    // 是两件事，把前者当成后者，一次瞬时抖动就会把徽标永久打成红叉。
    static RuntimeState merge(const RuntimeState &current,
                              const RuntimeProbe &probe);

    // 从运行时自报的输出（Python 的 `sys.executable`、Node 的
    // `process.execPath`）里取可执行文件路径；输出里没有绝对路径行时
    // 返回 fallback。路径只是展示信息，解析失败不值得判整次探测失败。
    static QString parseReportedPath(const QString &output,
                                     const QString &fallback);
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_ENVIRONMENTPROBE_H
