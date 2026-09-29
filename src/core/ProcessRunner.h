#ifndef AWB_CORE_PROCESSRUNNER_H
#define AWB_CORE_PROCESSRUNNER_H

#include <QByteArray>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace awb::core {

/// 同步 ProcessRunner::run() 的结果。
struct ProcessResult
{
    bool started = false;  ///< 进程是否起来了（超时被杀也算 true）
    int exitCode = -1;     ///< 进程退出码；未启动/超时保持 -1
    QString stdOut;        ///< stdout 解码后的文本
    QString stdErr;        ///< stderr 解码后的文本
    QString error;         ///< 未启动或超时时的可读原因，其余为空
};

/// 外部命令的机制层：不含业务知识——跑什么命令、输出怎么解读由调用方负责。
class ProcessRunner
{
public:
    // 经 PATH 解析裸程序名（Windows 上应用 PATHEXT，npm 风格的 .cmd/.bat
    // 垫片 "qwen" -> "qwen.cmd" 因此能找到）；找不到返回空串
    static QString findExecutable(const QString &program);

    // 分离启动 program args（本应用退出后子进程继续），PID 经 pid 返回。
    // program 须传已解析的绝对路径——Windows 的 .cmd/.bat 包装决策留在调用方。
    // outputFile 非空时子进程的 stdout 与 stderr 一并重定向到该文件
    // （每次启动截断）——agent 域靠这份捕获从 agent 自己的控制台输出里
    // 挑会话 URL（dsh 把每进程的 token URL 打在那里）。
    // 失败返回 false 并填 error。
    static bool startDetached(const QString &program, const QStringList &args,
                              qint64 *pid = nullptr, QString *error = nullptr,
                              const QString &workingDirectory = QString(),
                              const QProcessEnvironment &env = QProcessEnvironment(),
                              const QString &outputFile = QString());

    // 跑到结束并捕获两个通道。timeoutMs <= 0 表示无限等待；超时会杀进程，
    // error 注明超时，exitCode 保持 -1。
    static ProcessResult run(const QString &program, const QStringList &args,
                             int timeoutMs = 30000);

    // 杀掉整棵进程树（Windows: taskkill /F /T /PID，覆盖 cmd -> qwen.cmd ->
    // node 链）。与 0.3.0 一致地即发即忘：仅当杀进程命令本身没起来才返回 false。
    static bool killTree(qint64 pid);

    // killTree() 要跑的杀进程命令，拆成程序与参数供调用方先记日志再执行
    static QString killProgram();
    static QStringList killProgramArgs(qint64 pid);

    // 把原始命令行拆成程序 + 参数，遵守双引号规则（"a b" 是一个 token，
    // \" 是字面引号）。QProcess 在 Qt 6 提供了 QProcess::splitCommand，
    // 这是可移植的孪生实现。
    static QStringList splitCommand(const QString &command);

    // 解码子进程（npm/node/PowerShell 等）捕获到的字节。现代 CLI 输出
    // UTF-8；非法序列回退系统 locale 编码（zh-CN Windows 上是 GBK），
    // 老式 cmd 输出因此仍能解码而不是变乱码。
    static QString decodeOutput(const QByteArray &data);
};

} // namespace awb::core

#endif // AWB_CORE_PROCESSRUNNER_H
